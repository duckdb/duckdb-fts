#include "fts_indexing.hpp"
#include "fts_index_builder.hpp"
#include "fts_index_common.hpp"
#include "fts_index_maintenance.hpp"

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_search_path.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/client_data.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/extension_manager.hpp"
#include "duckdb/parser/constraints/not_null_constraint.hpp"
#include "duckdb/parser/constraints/unique_constraint.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/storage/storage_manager.hpp"

namespace duckdb {

static QualifiedName GetQualifiedName(ClientContext &context,
                                      const string &qname_str) {
  auto qname = QualifiedName::Parse(qname_str);
  if (qname.Schema().empty()) {
    vector<Identifier> schema_path;
    if (!qname.Catalog().empty()) {
      schema_path.push_back(qname.Catalog());
    }
    schema_path.push_back(
        ClientData::Get(context).catalog_search_path->GetDefaultSchema(
            context, qname.Catalog()));
    qname = qname.WithQualification(std::move(schema_path));
  }
  return qname;
}

static bool TableExists(ClientContext &context, const QualifiedName &qname) {
  return Catalog::GetEntry<TableCatalogEntry>(
             context, qname, OnEntryNotFound::RETURN_NULL) != nullptr;
}

static bool SupportsFTSTriggers(ClientContext &context,
                                const QualifiedName &qname) {
  auto &catalog = Catalog::GetCatalog(context, qname.Catalog());
  auto &attached = catalog.GetAttached();
  if (!attached.HasStorageManager()) {
    return true;
  }
  auto &storage_manager = attached.GetStorageManager();
  return storage_manager.InMemory() ||
         storage_manager.GetStorageVersion() >= StorageVersion::V2_0_0;
}

static bool ColumnHasNotNullConstraint(const TableCatalogEntry &table,
                                       const string &column_name) {
  auto column_identifier = Identifier(column_name);
  auto column_index = table.GetColumnIndex(column_identifier);
  for (auto &constraint : table.GetConstraints()) {
    if (constraint->type == ConstraintType::NOT_NULL) {
      auto &not_null = constraint->Cast<NotNullConstraint>();
      if (not_null.index == column_index) {
        return true;
      }
    } else if (constraint->type == ConstraintType::UNIQUE) {
      auto &unique = constraint->Cast<UniqueConstraint>();
      if (!unique.IsPrimaryKey()) {
        continue;
      }
      for (auto &pk_index : unique.GetLogicalIndexes(table.GetColumns())) {
        if (pk_index == column_index) {
          return true;
        }
      }
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// PRAGMA implementations
// ---------------------------------------------------------------------------

string FTSIndexing::DropFTSIndexQuery(ClientContext &context,
                                      const FunctionParameters &parameters) {
  auto qname =
      GetQualifiedName(context, StringValue::Get(parameters.values[0]));
  string fts_schema = GetFTSSchema(qname);

  if (!Catalog::GetSchema(context, qname.Catalog(),
                          Identifier(GetFTSSchemaName(qname)),
                          OnEntryNotFound::RETURN_NULL)) {
    throw CatalogException("a FTS index does not exist on table '%s.%s'. "
                           "Create one with 'PRAGMA create_fts_index()'.",
                           qname.Schema().GetIdentifierName(),
                           qname.Name().GetIdentifierName());
  }

  string result;
  if (TableExists(context, qname) && SupportsFTSTriggers(context, qname)) {
    result += FTSIndexMaintenance::DropTriggers(qname);
    result += "\n";
  }
  result += StringUtil::Format("DROP SCHEMA %s CASCADE;", fts_schema);
  return result;
}

string FTSIndexing::CreateFTSIndexQuery(ClientContext &context,
                                        const FunctionParameters &parameters) {
  auto qname =
      GetQualifiedName(context, StringValue::Get(parameters.values[0]));
  Catalog::GetEntry<TableCatalogEntry>(context, qname);

  // Named parameters
  auto get_string = [&](const string &name, const string &def) {
    auto it = parameters.named_parameters.find(Identifier(name));
    return it != parameters.named_parameters.end()
               ? StringValue::Get(it->second)
               : def;
  };
  auto get_bool = [&](const string &name, bool def) {
    auto it = parameters.named_parameters.find(Identifier(name));
    return it != parameters.named_parameters.end()
               ? BooleanValue::Get(it->second)
               : def;
  };
  auto has_named_parameter = [&](const string &name) {
    return parameters.named_parameters.find(Identifier(name)) !=
           parameters.named_parameters.end();
  };

  const string stemmer = get_string("stemmer", "porter");
  const string tokenizer = get_string("tokenizer", "regex");
  const string stopwords = get_string("stopwords", "english");
  const string ignore =
      get_string("ignore", "[0-9!@#$%^&*()_+={}\\[\\]:;<>,.?~\\\\/\\|''\"`-]+");
  if (tokenizer != "regex" && tokenizer != "opensearch_standard") {
    throw InvalidInputException(
        "Unrecognized tokenizer '%s'. Supported tokenizers are: ['regex', "
        "'opensearch_standard']",
        tokenizer);
  }

  const bool strip_accents =
      get_bool("strip_accents", tokenizer != "opensearch_standard");
  const bool lower = get_bool("lower", true);
  const bool overwrite = get_bool("overwrite", false);
  const bool incremental = get_bool("incremental", false);
  const bool layered_search = get_bool("layered_search", false);
  const bool cluster_terms = get_bool("cluster_terms", false) || layered_search;
  const bool structured_queries =
      ExtensionManager::Get(context).ExtensionIsLoaded("json");

  if (tokenizer == "opensearch_standard" && strip_accents &&
      has_named_parameter("strip_accents")) {
    throw InvalidInputException(
        "strip_accents=true is not supported with tokenizer "
        "'opensearch_standard'. OpenSearch's standard analyzer lowercases "
        "tokens but does not strip accents; use strip_accents=false.");
  }

  QualifiedName stopwords_table;
  if (stopwords != "english" && stopwords != "none") {
    stopwords_table = GetQualifiedName(context, stopwords);
    Catalog::GetEntry<TableCatalogEntry>(context, stopwords_table);
  }

  const string fts_schema = GetFTSSchema(qname);
  if (Catalog::GetSchema(context, qname.Catalog(),
                         Identifier(GetFTSSchemaName(qname)),
                         OnEntryNotFound::RETURN_NULL) &&
      !overwrite) {
    throw CatalogException("a FTS index already exists on table '%s.%s'. "
                           "Supply 'overwrite=1' to overwrite, or "
                           "drop the existing index with 'PRAGMA "
                           "drop_fts_index()' before creating a new one.",
                           qname.Schema().GetIdentifierName(),
                           qname.Name().GetIdentifierName());
  }

  // Positional parameters: table, id column, value column(s)
  const string doc_id = StringValue::Get(parameters.values[1]);
  auto &table = Catalog::GetEntry<TableCatalogEntry>(context, qname);
  if (!table.ColumnExists(Identifier(doc_id))) {
    throw CatalogException("Table '%s.%s' does not have a column named '%s'!",
                           qname.Schema().GetIdentifierName(),
                           qname.Name().GetIdentifierName(), doc_id);
  }
  vector<string> doc_values;
  for (idx_t i = 2; i < parameters.values.size(); i++) {
    const string col_name = StringValue::Get(parameters.values[i]);
    if (col_name == "*") {
      doc_values.clear();
      for (auto &cd : table.GetColumns().Logical()) {
        if (cd.Type() == LogicalType::VARCHAR) {
          doc_values.push_back(cd.Name().GetIdentifierName());
        }
      }
      break;
    }
    if (!table.ColumnExists(Identifier(col_name))) {
      throw CatalogException("Table '%s.%s' does not have a column named '%s'!",
                             qname.Schema().GetIdentifierName(),
                             qname.Name().GetIdentifierName(), col_name);
    }
    doc_values.push_back(col_name);
  }
  if (doc_values.empty()) {
    throw InvalidInputException(
        "at least one column must be supplied for indexing!");
  }
  if (incremental && !SupportsFTSTriggers(context, qname)) {
    throw InvalidInputException(
        "incremental FTS indexes require trigger support. Persistent DuckDB "
        "databases must use storage version v2.0.0 or higher; reattach/create "
        "the database with STORAGE_VERSION 'v2.0.0', or omit incremental=true "
        "to create a rebuild-only FTS index.");
  }
  if (incremental && !ColumnHasNotNullConstraint(table, doc_id)) {
    throw InvalidInputException("incremental FTS indexes require the document "
                                "id column to be NOT NULL");
  }
  FTSIndexConfig config;
  config.input_table = qname;
  config.input_id = doc_id;
  config.input_values = std::move(doc_values);
  config.stemmer = stemmer;
  if (stopwords == "none") {
    config.stopwords_mode = FTSStopwordsMode::NONE;
  } else if (stopwords == "english") {
    config.stopwords_mode = FTSStopwordsMode::ENGLISH;
  } else {
    config.stopwords_mode = FTSStopwordsMode::TABLE;
    config.stopwords_table = std::move(stopwords_table);
  }
  config.tokenizer = tokenizer;
  config.ignore = ignore;
  config.strip_accents = strip_accents;
  config.lower = lower;
  config.incremental = incremental;
  config.cluster_terms = cluster_terms;
  config.layered_search = layered_search;
  config.structured_queries = structured_queries;
  auto drop_existing_triggers =
      TableExists(context, qname) && SupportsFTSTriggers(context, qname);
  return FTSIndexBuilder::Create(config, drop_existing_triggers);
}

string FTSIndexing::CreateFTSBooleanQueryMacrosQuery(
    ClientContext &context, const FunctionParameters &parameters) {
  if (!ExtensionManager::Get(context).ExtensionIsLoaded("json")) {
    throw InvalidInputException(
        "create_fts_boolean_query_macros requires the json extension to be "
        "loaded first");
  }

  auto qname =
      GetQualifiedName(context, StringValue::Get(parameters.values[0]));
  if (!Catalog::GetSchema(context, qname.Catalog(),
                          Identifier(GetFTSSchemaName(qname)),
                          OnEntryNotFound::RETURN_NULL)) {
    throw CatalogException("a FTS index does not exist on table '%s.%s'. "
                           "Create one with 'PRAGMA create_fts_index()'.",
                           qname.Schema().GetIdentifierName(),
                           qname.Name().GetIdentifierName());
  }
  auto term_stats_name =
      QualifiedName(qname.Catalog(), Identifier(GetFTSSchemaName(qname)),
                    Identifier("term_stats"));
  if (!Catalog::GetEntry<TableCatalogEntry>(context, term_stats_name,
                                            OnEntryNotFound::RETURN_NULL)) {
    throw InvalidInputException(
        "create_fts_boolean_query_macros requires an FTS index created with "
        "layered_search=true");
  }

  return FTSIndexBuilder::CreateStructuredQueryMacros(qname);
}

} // namespace duckdb
