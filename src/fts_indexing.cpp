#include "fts_indexing.hpp"
#include "fts_sql_assets.hpp"
#include "fts_sql_template.hpp"

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_search_path.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/sql_identifier.hpp"
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

static string GetFTSSchemaName(const QualifiedName &qname) {
  return StringUtil::Format("fts_%s_%s", qname.Schema().GetIdentifierName(),
                            qname.Name().GetIdentifierName());
}

static string GetFTSSchema(const QualifiedName &qname) {
  auto result =
      IsInvalidCatalog(qname.Catalog())
          ? string("")
          : SQLIdentifier::ToString(qname.Catalog().GetIdentifierName()) + ".";
  result += SQLIdentifier::ToString(GetFTSSchemaName(qname));
  return result;
}

static SQLTemplateArgument GetFTSSchemaArgument(const QualifiedName &qname) {
  vector<string> parts;
  if (!IsInvalidCatalog(qname.Catalog())) {
    parts.push_back(qname.Catalog().GetIdentifierName());
  }
  parts.push_back(GetFTSSchemaName(qname));
  return SQLTemplateArgument::QualifiedIdentifier(parts);
}

static SQLTemplateArgument
GetQualifiedTableArgument(const QualifiedName &qname) {
  vector<string> parts;
  if (!IsInvalidCatalog(qname.Catalog())) {
    parts.push_back(qname.Catalog().GetIdentifierName());
  }
  parts.push_back(qname.Schema().GetIdentifierName());
  parts.push_back(qname.Name().GetIdentifierName());
  return SQLTemplateArgument::QualifiedIdentifier(parts);
}

static string GetQualifiedTableName(const QualifiedName &qname) {
  vector<string> parts;
  if (!IsInvalidCatalog(qname.Catalog())) {
    parts.push_back(
        SQLIdentifier::ToString(qname.Catalog().GetIdentifierName()));
  }
  parts.push_back(SQLIdentifier::ToString(qname.Schema().GetIdentifierName()));
  parts.push_back(SQLIdentifier::ToString(qname.Name().GetIdentifierName()));
  return StringUtil::Join(parts, ".");
}

static vector<string> GetFTSInsertTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ai_", GetFTSSchemaName(qname));
  return {prefix + "00_docs", prefix + "10_dict_insert", prefix + "20_terms",
          prefix + "30_dict_df", prefix + "40_stats"};
}

static vector<string>
GetFTSClusteredInsertTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ai_", GetFTSSchemaName(qname));
  return {prefix + "00_docs", prefix + "10_dict_insert",
          prefix + "20_terms_store", prefix + "30_dict_df",
          prefix + "40_stats"};
}

static vector<string> GetFTSDeleteTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ad_", GetFTSSchemaName(qname));
  return {prefix + "00_dict_df", prefix + "10_terms", prefix + "15_stats",
          prefix + "20_docs", prefix + "30_dict_prune"};
}

static vector<string>
GetFTSClusteredDeleteTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ad_", GetFTSSchemaName(qname));
  return {prefix + "00_dict_df", prefix + "10_terms_store", prefix + "15_stats",
          prefix + "20_docs", prefix + "30_dict_prune"};
}

static vector<string>
GetFTSLayeredInsertTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ai_", GetFTSSchemaName(qname));
  return {prefix + "15_term_stats",           prefix + "16_term_stats_by_len",
          prefix + "17_term_grams",           prefix + "18_raw_dict",
          prefix + "19_term_prefixes",        prefix + "35_term_stats_df",
          prefix + "36_term_stats_by_len_df", prefix + "37_raw_dict_df"};
}

static vector<string>
GetFTSLayeredDeleteTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ad_", GetFTSSchemaName(qname));
  return {prefix + "05_raw_dict_df",
          prefix + "21_term_prefixes_prune",
          prefix + "22_raw_dict_prune",
          prefix + "23_term_stats_df",
          prefix + "24_term_stats_by_len_df",
          prefix + "25_term_grams_prune",
          prefix + "26_term_stats_by_len_prune",
          prefix + "27_term_stats_prune"};
}

static vector<string> GetFTSTriggerNames(const QualifiedName &qname) {
  auto result = GetFTSInsertTriggerNames(qname);
  auto layered_insert_triggers = GetFTSLayeredInsertTriggerNames(qname);
  result.insert(result.end(), layered_insert_triggers.begin(),
                layered_insert_triggers.end());
  auto delete_triggers = GetFTSDeleteTriggerNames(qname);
  result.insert(result.end(), delete_triggers.begin(), delete_triggers.end());
  auto clustered_insert_triggers = GetFTSClusteredInsertTriggerNames(qname);
  result.insert(result.end(), clustered_insert_triggers.begin(),
                clustered_insert_triggers.end());
  auto clustered_delete_triggers = GetFTSClusteredDeleteTriggerNames(qname);
  result.insert(result.end(), clustered_delete_triggers.begin(),
                clustered_delete_triggers.end());
  auto layered_delete_triggers = GetFTSLayeredDeleteTriggerNames(qname);
  result.insert(result.end(), layered_delete_triggers.begin(),
                layered_delete_triggers.end());
  // Indexes created before delta statistics used this delete trigger name.
  auto delete_prefix =
      StringUtil::Format("__fts_%s_ad_", GetFTSSchemaName(qname));
  result.push_back(delete_prefix + "40_stats");
  return result;
}

static string GetFTSBuildTermsTable(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_build_terms_" +
                                 GetFTSSchemaName(qname));
}

static string GetFTSBuildDictTable(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_build_dict_" + GetFTSSchemaName(qname));
}

static string GetFTSTermsStorageTable() {
  return SQLIdentifier::ToString("__terms_storage");
}

static string GetFTSBuildRawDictTable(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_build_raw_dict_" +
                                 GetFTSSchemaName(qname));
}

static string GetFTSTermStatsTermIndex(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_" + GetFTSSchemaName(qname) +
                                 "_term_stats_term_idx");
}

static string GetFTSTermGramsGramIndex(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_" + GetFTSSchemaName(qname) +
                                 "_term_grams_gram_idx");
}

static string GetFTSTermPrefixesPrefixIndex(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_" + GetFTSSchemaName(qname) +
                                 "_term_prefixes_prefix_idx");
}

static string FieldLengthAggregateList(idx_t field_count) {
  vector<string> field_lengths;
  for (idx_t field_id = 0; field_id < field_count; field_id++) {
    field_lengths.push_back(StringUtil::Format(
        "count(*) FILTER (WHERE fieldid = %i)::BIGINT", field_id));
  }
  return "list_value(" + StringUtil::Join(field_lengths, ", ") + ")";
}

static string ZeroFieldLengthList(idx_t field_count, const string &type) {
  vector<string> field_lengths;
  for (idx_t field_id = 0; field_id < field_count; field_id++) {
    field_lengths.push_back("0::" + type);
  }
  return "list_value(" + StringUtil::Join(field_lengths, ", ") + ")";
}

static string StatsFieldAggregateList(idx_t field_count,
                                      const string &aggregate,
                                      const string &fallback,
                                      const string &type) {
  vector<string> field_lengths;
  for (idx_t field_id = 0; field_id < field_count; field_id++) {
    auto expression = StringUtil::Format(
        "%s(list_extract(docs.field_lens, %i))", aggregate, field_id + 1);
    if (!fallback.empty()) {
      expression = StringUtil::Format("coalesce(%s, %s)", expression, fallback);
    }
    field_lengths.push_back(expression + "::" + type);
  }
  return "list_value(" + StringUtil::Join(field_lengths, ", ") + ")";
}

static string StatsDeltaFieldList(idx_t field_count, const string &operation,
                                  bool average) {
  vector<string> field_lengths;
  for (idx_t field_id = 0; field_id < field_count; field_id++) {
    auto value =
        StringUtil::Format("list_extract(fts_stats.total_field_lens, %i) %s "
                           "delta.field_len_%i",
                           field_id + 1, operation, field_id);
    if (average) {
      value = StringUtil::Format(
          "(%s)::DOUBLE / (fts_stats.num_docs %s delta.num_docs)", value,
          operation);
    } else {
      value = "(" + value + ")::HUGEINT";
    }
    field_lengths.push_back(value);
  }
  return "list_value(" + StringUtil::Join(field_lengths, ", ") + ")";
}

static string StatsDeltaUpdateStatement(idx_t field_count, bool insert) {
  auto operation = insert ? "+" : "-";
  auto transition_table = insert ? "fts_new_rows" : "fts_old_rows";
  vector<string> delta_field_lengths;
  for (idx_t field_id = 0; field_id < field_count; field_id++) {
    delta_field_lengths.push_back(StringUtil::Format(
        "coalesce(sum(list_extract(docs.field_lens, %i)), 0)::HUGEINT AS "
        "field_len_%i",
        field_id + 1, field_id));
  }

  // clang-format off
	string result = R"(
            UPDATE %fts_schema%.stats AS fts_stats
            SET num_docs = fts_stats.num_docs %operation% delta.num_docs,
                avgdl = CASE
                    WHEN fts_stats.num_docs %operation% delta.num_docs = 0 THEN NULL
                    ELSE (
                        fts_stats.total_len %operation% delta.total_len
                    )::DOUBLE / (
                        fts_stats.num_docs %operation% delta.num_docs
                    )
                END,
                avg_field_lens = CASE
                    WHEN fts_stats.num_docs %operation% delta.num_docs = 0
                        THEN NULL::DOUBLE[]
                    ELSE %average_field_lengths%
                END,
                total_len = fts_stats.total_len %operation% delta.total_len,
                total_field_lens = %total_field_lengths%
            FROM (
                SELECT count(*)::BIGINT AS num_docs,
                       coalesce(sum(docs.len), 0)::HUGEINT AS total_len,
                       %delta_field_lengths%
                FROM %fts_schema%.docs AS docs
                JOIN %transition_table% AS changed_rows
                  ON docs.name = changed_rows.%input_id%
            ) AS delta
    )";
  // clang-format on

  result = StringUtil::Replace(result, "%operation%", operation);
  result =
      StringUtil::Replace(result, "%average_field_lengths%",
                          StatsDeltaFieldList(field_count, operation, true));
  result =
      StringUtil::Replace(result, "%total_field_lengths%",
                          StatsDeltaFieldList(field_count, operation, false));
  result = StringUtil::Replace(
      result, "%delta_field_lengths%",
      StringUtil::Join(delta_field_lengths, ",\n                       "));
  return StringUtil::Replace(result, "%transition_table%", transition_table);
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
// SQL script builder helpers
// ---------------------------------------------------------------------------

static string SchemaSetupScript(const QualifiedName &qname) {
  return RenderSQLTemplate(fts_sql::SCHEMA_SETUP,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string StopwordsScript(ClientContext &context,
                              const QualifiedName &qname,
                              const string &stopwords) {
  if (stopwords == "none") {
    return "";
  }
  if (stopwords == "english") {
    // Default list of English stopwords from "The SMART system".
    return RenderSQLTemplate(fts_sql::ENGLISH_STOPWORDS,
                             {{"fts_schema", GetFTSSchemaArgument(qname)}});
  }
  auto stopwords_qname = GetQualifiedName(context, stopwords);
  return RenderSQLTemplate(
      fts_sql::IMPORT_STOPWORDS,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"stopwords_table", GetQualifiedTableArgument(stopwords_qname)}});
}

static string NormalizeTokenInputExpression(bool strip_accents, bool lower) {
  string expr = "s::VARCHAR";
  if (strip_accents) {
    expr = "strip_accents(" + expr + ")";
  }
  if (lower) {
    expr = "lower(" + expr + ")";
  }
  return expr;
}

static string TokenizeMacroScript(const QualifiedName &qname,
                                  const string &tokenizer, const string &ignore,
                                  bool strip_accents, bool lower) {
  auto expr = NormalizeTokenInputExpression(strip_accents, lower);
  if (tokenizer == "opensearch_standard") {
    expr = "fts_tokenize_opensearch_standard(" + expr + ")";
  } else {
    auto delimiter = "(" + ignore + ")|\\s+";
    expr = "string_split_regex(" + expr + ", " +
           SQLString::ToString(delimiter) + ")";
  }
  return RenderSQLTemplate(
      fts_sql::TOKENIZE_MACRO,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"token_expression", SQLTemplateArgument::TrustedSQL(expr)}});
}

static string IndexTablesScript(const QualifiedName &qname,
                                const string &input_id,
                                const vector<string> &input_values,
                                const string &stemmer, const string &stopwords,
                                const string &build_terms_table,
                                const string &build_dict_table,
                                const string &build_raw_dict_table,
                                bool cluster_terms, bool layered_search) {
  string term_expression =
      stemmer == "none" ? "t.w"
                        : "stem(t.w, " + SQLString::ToString(stemmer) + ")";
  string stopwords_filter = stopwords == "none"
                                ? string("")
                                : "AND t.w NOT IN (SELECT sw FROM " +
                                      GetFTSSchema(qname) + ".stopwords)";

  vector<string> field_values;
  vector<string> tokenize_fields;
  for (idx_t i = 0; i < input_values.size(); i++) {
    field_values.push_back(StringUtil::Format(
        "(%i, %s)", i, SQLString::ToString(input_values[i])));
    tokenize_fields.push_back(RenderSQLTemplate(
        fts_sql::TOKENIZE_FIELD,
        {{"fts_schema", GetFTSSchemaArgument(qname)},
         {"input_value", SQLTemplateArgument::Identifier(input_values[i])},
         {"field_id", SQLTemplateArgument::Integer(static_cast<int64_t>(i))},
         {"input_table", GetQualifiedTableArgument(qname)}}));
  }
  string build_raw_dict;
  string raw_dict_table;
  if (layered_search) {
    build_raw_dict = RenderSQLTemplate(
        fts_sql::BUILD_RAW_DICT,
        {{"build_raw_dict_table",
          SQLTemplateArgument::TrustedSQL(build_raw_dict_table)},
         {"build_terms_table",
          SQLTemplateArgument::TrustedSQL(build_terms_table)},
         {"build_dict_table",
          SQLTemplateArgument::TrustedSQL(build_dict_table)}});
    raw_dict_table = RenderSQLTemplate(
        fts_sql::RAW_DICT_TABLE,
        {{"fts_schema", GetFTSSchemaArgument(qname)},
         {"build_raw_dict_table",
          SQLTemplateArgument::TrustedSQL(build_raw_dict_table)}});
  }
  auto raw_dict_join =
      layered_search ? "JOIN temp." + build_raw_dict_table +
                           " AS build_raw_dict ON "
                           "build_raw_dict.raw_term = build_terms.raw_term AND "
                           "build_raw_dict.termid = build_dict.termid"
                     : "";
  auto terms_order_by =
      cluster_terms
          ? (layered_search
                 ? "ORDER BY build_dict.termid, build_raw_dict.rawtermid, "
                   "build_terms.fieldid, build_terms.docid"
                 : "ORDER BY build_dict.termid, build_terms.fieldid, "
                   "build_terms.docid")
          : "";
  return RenderSQLTemplate(
      fts_sql::INDEX_TABLES,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"field_values",
        SQLTemplateArgument::TrustedSQL(StringUtil::Join(field_values, ", "))},
       {"build_terms_table",
        SQLTemplateArgument::TrustedSQL(build_terms_table)},
       {"build_dict_table", SQLTemplateArgument::TrustedSQL(build_dict_table)},
       {"build_raw_dict_table",
        SQLTemplateArgument::TrustedSQL(build_raw_dict_table)},
       {"union_fields_query", SQLTemplateArgument::TrustedSQL(StringUtil::Join(
                                  tokenize_fields, " UNION ALL "))},
       {"term_expression", SQLTemplateArgument::TrustedSQL(term_expression)},
       {"raw_term_select", SQLTemplateArgument::TrustedSQL(
                               layered_search ? "t.w AS raw_term," : "")},
       {"stopwords_filter", SQLTemplateArgument::TrustedSQL(stopwords_filter)},
       {"raw_term_output",
        SQLTemplateArgument::TrustedSQL(layered_search ? "ss.raw_term," : "")},
       {"field_length_aggregates",
        SQLTemplateArgument::TrustedSQL(
            FieldLengthAggregateList(input_values.size()))},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"zero_field_lengths",
        SQLTemplateArgument::TrustedSQL(
            ZeroFieldLengthList(input_values.size(), "BIGINT"))},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"build_raw_dict", SQLTemplateArgument::TrustedSQL(build_raw_dict)},
       {"rawtermid_select",
        SQLTemplateArgument::TrustedSQL(
            layered_search ? "build_raw_dict.rawtermid," : "")},
       {"raw_dict_join", SQLTemplateArgument::TrustedSQL(raw_dict_join)},
       {"terms_order_by", SQLTemplateArgument::TrustedSQL(terms_order_by)},
       {"raw_dict_table", SQLTemplateArgument::TrustedSQL(raw_dict_table)},
       {"average_field_lengths",
        SQLTemplateArgument::TrustedSQL(
            StatsFieldAggregateList(input_values.size(), "avg", "", "DOUBLE"))},
       {"total_field_lengths",
        SQLTemplateArgument::TrustedSQL(StatsFieldAggregateList(
            input_values.size(), "sum", "0", "HUGEINT"))}});
}

static string LayeredSidecarScript(const QualifiedName &qname) {
  return RenderSQLTemplate(
      fts_sql::LAYERED_SIDECARS,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"term_stats_term_index",
        SQLTemplateArgument::TrustedSQL(GetFTSTermStatsTermIndex(qname))},
       {"term_grams_gram_index",
        SQLTemplateArgument::TrustedSQL(GetFTSTermGramsGramIndex(qname))},
       {"term_prefixes_prefix_index",
        SQLTemplateArgument::TrustedSQL(
            GetFTSTermPrefixesPrefixIndex(qname))}});
}

static string
FieldScoringValidationCTEs(const QualifiedName &qname,
                           const string &params_name = "params",
                           const string &field_weights_name = "field_weights",
                           const string &field_b_name = "field_b",
                           const string &scoring_model_name = "scoring_model",
                           const string &tie_breaker_name = "tie_breaker",
                           const string &default_b_name = "default_b") {
  return RenderSQLTemplate(
      fts_sql::FIELD_SCORING_VALIDATION_CTES,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"params", SQLTemplateArgument::Identifier(params_name)},
       {"field_weights", SQLTemplateArgument::Identifier(field_weights_name)},
       {"field_b", SQLTemplateArgument::Identifier(field_b_name)},
       {"scoring_model", SQLTemplateArgument::Identifier(scoring_model_name)},
       {"tie_breaker", SQLTemplateArgument::Identifier(tie_breaker_name)},
       {"default_b", SQLTemplateArgument::Identifier(default_b_name)}});
}

static string FieldScoringConfigCTEs(const QualifiedName &qname) {
  return RenderSQLTemplate(
      fts_sql::FIELD_SCORING_CONFIG_CTES,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"field_scoring_validation_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringValidationCTEs(qname))}});
}

static string FieldScoringScoreCTEs(const QualifiedName &qname) {
  return RenderSQLTemplate(fts_sql::FIELD_SCORING_SCORE_CTES,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string MatchMacroScript(const QualifiedName &qname,
                               const string &stemmer) {
  return RenderSQLTemplate(
      fts_sql::MATCH_BM25,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"stemmer", SQLTemplateArgument::StringLiteral(stemmer)},
       {"field_scoring_config_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringConfigCTEs(qname))},
       {"field_scoring_score_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringScoreCTEs(qname))}});
}

static string LayeredSearchTableMacroScript(const QualifiedName &qname,
                                            const string &stemmer) {
  return RenderSQLTemplate(
      fts_sql::SEARCH_LAYERED_BM25,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"stemmer", SQLTemplateArgument::StringLiteral(stemmer)},
       {"field_scoring_config_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringConfigCTEs(qname))},
       {"field_scoring_score_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringScoreCTEs(qname))}});
}

static string LayeredMatchMacroScript(const QualifiedName &qname) {
  return RenderSQLTemplate(fts_sql::MATCH_LAYERED_BM25,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string
StructuredLayeredSearchTableMacroScript(const QualifiedName &qname) {
  return RenderSQLTemplate(
      fts_sql::SEARCH_LAYERED_BM25_QUERY,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"field_scoring_validation_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringValidationCTEs(
            qname, "query_params", "scoring_weights", "scoring_field_b",
            "scoring_model", "scoring_tie_breaker", "bm25_b"))}});
}

static string StructuredLayeredMatchMacroScript(const QualifiedName &qname) {
  return RenderSQLTemplate(fts_sql::MATCH_LAYERED_BM25_QUERY,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string StructuredLayeredSearchMacroScript(const QualifiedName &qname) {
  return StructuredLayeredSearchTableMacroScript(qname) +
         StructuredLayeredMatchMacroScript(qname);
}

static string LayeredSearchMacroScript(const QualifiedName &qname,
                                       const string &stemmer,
                                       bool include_structured_queries) {
  auto result = LayeredSearchTableMacroScript(qname, stemmer) +
                LayeredMatchMacroScript(qname);
  if (include_structured_queries) {
    result += StructuredLayeredSearchMacroScript(qname);
  }
  return result;
}

static string DropFTSTriggersScript(const QualifiedName &qname) {
  vector<string> statements;
  auto input_table = GetQualifiedTableName(qname);
  for (auto &trigger_name : GetFTSTriggerNames(qname)) {
    statements.push_back(
        StringUtil::Format("DROP TRIGGER IF EXISTS %s ON %s;",
                           SQLIdentifier::ToString(trigger_name), input_table));
  }
  return StringUtil::Join(statements, "\n");
}

static string IncrementalIndexSetupScript(const QualifiedName &qname) {
  auto index_name =
      StringUtil::Format("__fts_%s_docs_name_idx", GetFTSSchemaName(qname));
  return StringUtil::Format(
      "CREATE UNIQUE INDEX %s ON %%fts_schema%%.docs(name);",
      SQLIdentifier::ToString(index_name));
}

static string ClusteredIncrementalIndexSetupScript() {
  return StringUtil::Format(
      "CREATE TABLE %%fts_schema%%.%s AS SELECT termid, docid, fieldid FROM "
      "%%fts_schema%%.terms;\n"
      "DROP TABLE %%fts_schema%%.terms;\n"
      "CREATE VIEW %%fts_schema%%.terms AS SELECT termid, docid, fieldid FROM "
      "%%fts_schema%%.%s;",
      GetFTSTermsStorageTable(), GetFTSTermsStorageTable());
}

static string LayeredAffectedTermIdsSubquery(const string &token_ctes) {
  // clang-format off
	return token_ctes + R"(
            SELECT DISTINCT d.termid
            FROM stemmed_stopped AS ss
            JOIN %fts_schema%.dict AS d ON ss.term = d.term
        )";
  // clang-format on
}

static string LayeredInsertNewTermsTriggerScript(const QualifiedName &qname,
                                                 const string &token_ctes) {
  // clang-format off
	string result = R"(
        CREATE TRIGGER %trigger_15_term_stats% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.term_stats (termid, term, df, term_len, gram_count)
            %token_ctes%,
            affected_terms AS (
                SELECT DISTINCT d.termid,
                       d.term,
                       d.df
                FROM stemmed_stopped AS ss
                JOIN %fts_schema%.dict AS d ON ss.term = d.term
                WHERE d.term <> ''
            )
            SELECT affected_terms.termid,
                   affected_terms.term,
                   affected_terms.df,
                   length(affected_terms.term)::BIGINT AS term_len,
                   greatest(length(affected_terms.term) - 2, 0)::BIGINT AS gram_count
            FROM affected_terms
            WHERE affected_terms.term <> ''
              AND NOT EXISTS (
                  SELECT 1
                  FROM %fts_schema%.term_stats AS ts
                  WHERE ts.termid = affected_terms.termid
              )
            ORDER BY affected_terms.termid;

        CREATE TRIGGER %trigger_16_term_stats_by_len% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.term_stats_by_len (termid, term, df, term_len, gram_count)
            %token_ctes%,
            affected_terms AS (
                SELECT DISTINCT d.termid
                FROM stemmed_stopped AS ss
                JOIN %fts_schema%.dict AS d ON ss.term = d.term
            )
            SELECT ts.termid,
                   ts.term,
                   ts.df,
                   ts.term_len,
                   ts.gram_count
            FROM %fts_schema%.term_stats AS ts
            JOIN affected_terms USING (termid)
            WHERE NOT EXISTS (
                SELECT 1
                FROM %fts_schema%.term_stats_by_len AS tsl
                WHERE tsl.termid = ts.termid
            )
            ORDER BY ts.term_len,
                     ts.df,
                     ts.termid;

        CREATE TRIGGER %trigger_17_term_grams% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.term_grams (gram, termid)
            %token_ctes%,
            affected_terms AS (
                SELECT DISTINCT d.termid
                FROM stemmed_stopped AS ss
                JOIN %fts_schema%.dict AS d ON ss.term = d.term
            ),
            grams AS (
                SELECT 'g' || lower(hex(substr(ts.term, i, 3))) AS gram,
                       ts.termid
                FROM %fts_schema%.term_stats AS ts,
                     affected_terms AS affected_terms,
                     range(1, ts.gram_count + 1) AS r(i)
                WHERE ts.termid = affected_terms.termid
                  AND ts.gram_count > 0
                  AND NOT regexp_full_match(ts.term, '[0-9]+')
            )
            SELECT grams.gram,
                   grams.termid
            FROM grams
            WHERE NOT EXISTS (
                SELECT 1
                FROM %fts_schema%.term_grams AS tg
                WHERE tg.termid = grams.termid
                  AND tg.gram = grams.gram
            )
            ORDER BY grams.gram,
                     grams.termid;

        CREATE TRIGGER %trigger_18_raw_dict% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.raw_dict (rawtermid, raw_term, termid, df)
            %token_ctes%,
            new_raw_terms AS (
                SELECT DISTINCT ss.raw_term,
                       d.termid
                FROM stemmed_stopped AS ss
                JOIN %fts_schema%.dict AS d ON d.term = ss.term
                WHERE NOT EXISTS (
                    SELECT 1
                    FROM %fts_schema%.raw_dict AS rd
                    WHERE rd.raw_term = ss.raw_term
                      AND rd.termid = d.termid
                )
                ORDER BY ss.raw_term,
                         d.termid
            )
            SELECT (SELECT COALESCE(max(rawtermid) + 1, 0) FROM %fts_schema%.raw_dict)
                       + row_number() OVER () - 1 AS rawtermid,
                   new_raw_terms.raw_term,
                   new_raw_terms.termid,
                   0 AS df
            FROM new_raw_terms;

        CREATE TRIGGER %trigger_19_term_prefixes% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.term_prefixes (prefix_len, prefix, rawtermid)
            %token_ctes%,
            affected_raw_terms AS (
                SELECT DISTINCT rd.rawtermid,
                       rd.raw_term
                FROM stemmed_stopped AS ss
                JOIN %fts_schema%.dict AS d ON d.term = ss.term
                JOIN %fts_schema%.raw_dict AS rd
                  ON rd.raw_term = ss.raw_term
                 AND rd.termid = d.termid
            )
            SELECT prefix_lengths.prefix_len,
                   substr(affected_raw_terms.raw_term, 1, prefix_lengths.prefix_len) AS prefix,
                   affected_raw_terms.rawtermid
            FROM affected_raw_terms,
                 (VALUES (2::UTINYINT), (3::UTINYINT)) AS prefix_lengths(prefix_len)
            WHERE length(affected_raw_terms.raw_term) >= prefix_lengths.prefix_len
              AND NOT EXISTS (
                  SELECT 1
                  FROM %fts_schema%.term_prefixes AS tp
                  WHERE tp.prefix_len = prefix_lengths.prefix_len
                    AND tp.prefix = substr(affected_raw_terms.raw_term, 1, prefix_lengths.prefix_len)
                    AND tp.rawtermid = affected_raw_terms.rawtermid
              )
            ORDER BY prefix_lengths.prefix_len,
                     prefix,
                     affected_raw_terms.rawtermid;
    )";
  // clang-format on

  auto trigger_names = GetFTSLayeredInsertTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_15_term_stats%",
                               SQLIdentifier::ToString(trigger_names[0]));
  result = StringUtil::Replace(result, "%trigger_16_term_stats_by_len%",
                               SQLIdentifier::ToString(trigger_names[1]));
  result = StringUtil::Replace(result, "%trigger_17_term_grams%",
                               SQLIdentifier::ToString(trigger_names[2]));
  result = StringUtil::Replace(result, "%trigger_18_raw_dict%",
                               SQLIdentifier::ToString(trigger_names[3]));
  result = StringUtil::Replace(result, "%trigger_19_term_prefixes%",
                               SQLIdentifier::ToString(trigger_names[4]));
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(result, "%token_ctes%", token_ctes);
  return result;
}

static string LayeredInsertDFTriggerScript(const QualifiedName &qname,
                                           const string &token_ctes,
                                           const string &input_id) {
  // clang-format off
	string result = R"(
        CREATE TRIGGER %trigger_35_term_stats_df% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.term_stats AS ts
            SET df = d.df
            FROM %fts_schema%.dict AS d
            WHERE ts.termid = d.termid
              AND ts.termid IN (
                  %affected_termids%
              );

        CREATE TRIGGER %trigger_36_term_stats_by_len_df% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.term_stats_by_len AS ts
            SET df = d.df
            FROM %fts_schema%.dict AS d
            WHERE ts.termid = d.termid
              AND ts.termid IN (
                  %affected_termids%
              );

        CREATE TRIGGER %trigger_37_raw_dict_df% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.raw_dict AS rd
            SET df = rd.df + inserted_df.df
            FROM (
                SELECT t.rawtermid,
                       count(DISTINCT t.docid)::BIGINT AS df
                FROM %fts_schema%.terms AS t
                JOIN %fts_schema%.docs AS docs ON docs.docid = t.docid
                JOIN fts_new_rows AS new_rows ON new_rows.%input_id% = docs.name
                GROUP BY t.rawtermid
            ) AS inserted_df
            WHERE rd.rawtermid = inserted_df.rawtermid;
    )";
  // clang-format on

  auto affected_termids = LayeredAffectedTermIdsSubquery(token_ctes);
  auto trigger_names = GetFTSLayeredInsertTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_35_term_stats_df%",
                               SQLIdentifier::ToString(trigger_names[5]));
  result = StringUtil::Replace(result, "%trigger_36_term_stats_by_len_df%",
                               SQLIdentifier::ToString(trigger_names[6]));
  result = StringUtil::Replace(result, "%trigger_37_raw_dict_df%",
                               SQLIdentifier::ToString(trigger_names[7]));
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  result = StringUtil::Replace(result, "%affected_termids%", affected_termids);
  return result;
}

static string LayeredDeleteBeforeTermsTriggerScript(const QualifiedName &qname,
                                                    const string &input_id) {
  // clang-format off
	string result = R"(
        CREATE TRIGGER %trigger_05_raw_dict_df% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.raw_dict AS rd
            SET df = rd.df - deleted_df.df
            FROM (
                SELECT t.rawtermid,
                       count(DISTINCT t.docid)::BIGINT AS df
                FROM %fts_schema%.terms AS t
                JOIN %fts_schema%.docs AS docs ON docs.docid = t.docid
                JOIN fts_old_rows AS old_rows ON old_rows.%input_id% = docs.name
                GROUP BY t.rawtermid
            ) AS deleted_df
            WHERE rd.rawtermid = deleted_df.rawtermid;
    )";
  // clang-format on

  auto trigger_names = GetFTSLayeredDeleteTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_05_raw_dict_df%",
                               SQLIdentifier::ToString(trigger_names[0]));
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  return result;
}

static string LayeredDeleteAfterDFTriggerScript(const QualifiedName &qname,
                                                const string &token_ctes) {
  // clang-format off
	string result = R"(
        CREATE TRIGGER %trigger_21_term_prefixes_prune% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.term_prefixes
            WHERE rawtermid IN (
                SELECT rd.rawtermid
                FROM %fts_schema%.raw_dict AS rd
                WHERE rd.df = 0
            );

        CREATE TRIGGER %trigger_22_raw_dict_prune% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.raw_dict
            WHERE df = 0;

        CREATE TRIGGER %trigger_23_term_stats_df% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.term_stats AS ts
            SET df = d.df
            FROM %fts_schema%.dict AS d
            WHERE ts.termid = d.termid
              AND ts.termid IN (
                  %affected_termids%
              );

        CREATE TRIGGER %trigger_24_term_stats_by_len_df% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.term_stats_by_len AS ts
            SET df = d.df
            FROM %fts_schema%.dict AS d
            WHERE ts.termid = d.termid
              AND ts.termid IN (
                  %affected_termids%
              );

        CREATE TRIGGER %trigger_25_term_grams_prune% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.term_grams
            WHERE termid IN (
                SELECT d.termid
                FROM %fts_schema%.dict AS d
                WHERE d.df = 0
                  AND d.termid IN (
                      %affected_termids%
                  )
            );

        CREATE TRIGGER %trigger_26_term_stats_by_len_prune% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.term_stats_by_len
            WHERE termid IN (
                SELECT d.termid
                FROM %fts_schema%.dict AS d
                WHERE d.df = 0
                  AND d.termid IN (
                      %affected_termids%
                  )
            );

        CREATE TRIGGER %trigger_27_term_stats_prune% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.term_stats
            WHERE termid IN (
                SELECT d.termid
                FROM %fts_schema%.dict AS d
                WHERE d.df = 0
                  AND d.termid IN (
                      %affected_termids%
                  )
            );
    )";
  // clang-format on

  auto affected_termids = LayeredAffectedTermIdsSubquery(token_ctes);
  auto trigger_names = GetFTSLayeredDeleteTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_21_term_prefixes_prune%",
                               SQLIdentifier::ToString(trigger_names[1]));
  result = StringUtil::Replace(result, "%trigger_22_raw_dict_prune%",
                               SQLIdentifier::ToString(trigger_names[2]));
  result = StringUtil::Replace(result, "%trigger_23_term_stats_df%",
                               SQLIdentifier::ToString(trigger_names[3]));
  result = StringUtil::Replace(result, "%trigger_24_term_stats_by_len_df%",
                               SQLIdentifier::ToString(trigger_names[4]));
  result = StringUtil::Replace(result, "%trigger_25_term_grams_prune%",
                               SQLIdentifier::ToString(trigger_names[5]));
  result = StringUtil::Replace(result, "%trigger_26_term_stats_by_len_prune%",
                               SQLIdentifier::ToString(trigger_names[6]));
  result = StringUtil::Replace(result, "%trigger_27_term_stats_prune%",
                               SQLIdentifier::ToString(trigger_names[7]));
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(result, "%affected_termids%", affected_termids);
  return result;
}

static string InsertTriggerScript(const QualifiedName &qname,
                                  const string &input_id,
                                  const vector<string> &input_values,
                                  bool layered_search) {
  // clang-format off
	string docs_new_docs_cte = R"(
        fts_new_docs AS (
            SELECT COALESCE((SELECT max(docid) + 1 FROM %fts_schema%.docs), 0)
                       + row_number() OVER (ORDER BY fts_new_rows.%input_id%) - 1 AS docid,
                   fts_new_rows.%input_id% AS name,
                   %input_value_select_list%
            FROM fts_new_rows
        )
    )";

	string token_new_docs_cte = R"(
        fts_new_docs AS (
            SELECT (SELECT max(docid) - (SELECT count(*) FROM fts_new_rows) + 1 FROM %fts_schema%.docs)
                       + row_number() OVER (ORDER BY fts_new_rows.%input_id%) - 1 AS docid,
                   fts_new_rows.%input_id% AS name,
                   %input_value_select_list%
            FROM fts_new_rows
        )
    )";

	string tokenize_field_query = R"(
        SELECT unnest(%fts_schema%.tokenize(fts_ii.%input_value%)) AS w,
               fts_ii.docid AS docid,
               (SELECT fieldid FROM %fts_schema%.fields WHERE field = %input_value_string%) AS fieldid
        FROM fts_new_docs AS fts_ii
    )";

	string token_ctes = R"(
        WITH %new_docs_cte%,
        tokenized AS (
            %union_fields_query%
        ),
        stemmed_stopped AS (
            SELECT t.w AS raw_term,
                   stem(t.w, '%stemmer%') AS term,
                   t.docid AS docid,
                   t.fieldid AS fieldid
            FROM tokenized AS t
            WHERE t.w NOT NULL
              AND t.w <> ''
              AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)
        )
    )";

	string result = R"(
        CREATE TRIGGER %trigger_00_docs% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.docs (docid, name, len, field_lens)
            WITH %docs_new_docs_cte%,
            tokenized AS (
                %union_fields_query%
            ),
            stemmed_stopped AS (
                SELECT stem(t.w, '%stemmer%') AS term,
                       t.docid AS docid,
                       t.fieldid AS fieldid
                FROM tokenized AS t
                WHERE t.w NOT NULL
                  AND t.w <> ''
                  AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)
            ),
            lengths AS (
                SELECT docid,
                       count(*)::BIGINT AS len,
                       %field_length_aggregates% AS field_lens
                FROM stemmed_stopped
                GROUP BY docid
            )
            SELECT nd.docid,
                   nd.name,
                   coalesce(lengths.len, 0)::BIGINT AS len,
                   coalesce(lengths.field_lens, %zero_field_lengths%)::BIGINT[] AS field_lens
            FROM fts_new_docs AS nd
            LEFT JOIN lengths ON lengths.docid = nd.docid;

        CREATE TRIGGER %trigger_10_dict_insert% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.dict (termid, term, df)
            %token_ctes%,
            new_terms AS (
                SELECT DISTINCT term
                FROM stemmed_stopped
                WHERE term NOT IN (SELECT term FROM %fts_schema%.dict)
                ORDER BY term
            )
            SELECT (SELECT COALESCE(max(termid) + 1, 0) FROM %fts_schema%.dict) + row_number() OVER () - 1 AS termid,
                   term,
                   0 AS df
            FROM new_terms;

        %layered_insert_new_terms_triggers%

        CREATE TRIGGER %trigger_20_terms% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.terms (%terms_insert_columns%)
            %token_ctes%
            SELECT ss.docid,
                   ss.fieldid,
                   d.termid
                   %rawtermid_select%
            FROM stemmed_stopped AS ss
            JOIN %fts_schema%.dict AS d ON ss.term = d.term
            %raw_dict_join%
            %insert_terms_order_by%;

        CREATE TRIGGER %trigger_30_dict_df% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.dict AS d
            SET df = d.df + inserted_df_delta.df_delta
            FROM (
                SELECT t.termid,
                       COUNT(DISTINCT t.docid) AS df_delta
                FROM %fts_schema%.terms AS t
                JOIN %fts_schema%.docs AS docs ON t.docid = docs.docid
                JOIN fts_new_rows AS new_rows ON docs.name = new_rows.%input_id%
                GROUP BY t.termid
            ) AS inserted_df_delta
            WHERE d.termid = inserted_df_delta.termid;

        %layered_insert_df_triggers%

        CREATE TRIGGER %trigger_40_stats% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            %stats_delta_update%;
    )";
  // clang-format on

  vector<string> input_value_selects;
  vector<string> tokenize_fields;
  for (auto &input_value : input_values) {
    input_value_selects.push_back(StringUtil::Format(
        "fts_new_rows.%s AS %s", SQLIdentifier::ToString(input_value),
        SQLIdentifier::ToString(input_value)));
    auto query = StringUtil::Replace(tokenize_field_query, "%input_value%",
                                     SQLIdentifier::ToString(input_value));
    query = StringUtil::Replace(query, "%input_value_string%",
                                SQLString::ToString(input_value));
    tokenize_fields.push_back(query);
  }

  docs_new_docs_cte = StringUtil::Replace(
      docs_new_docs_cte, "%input_value_select_list%",
      StringUtil::Join(input_value_selects, ",\n                   "));
  token_new_docs_cte = StringUtil::Replace(
      token_new_docs_cte, "%input_value_select_list%",
      StringUtil::Join(input_value_selects, ",\n                   "));
  token_ctes =
      StringUtil::Replace(token_ctes, "%new_docs_cte%", token_new_docs_cte);
  token_ctes =
      StringUtil::Replace(token_ctes, "%union_fields_query%",
                          StringUtil::Join(tokenize_fields, " UNION ALL "));
  result =
      StringUtil::Replace(result, "%docs_new_docs_cte%", docs_new_docs_cte);
  result =
      StringUtil::Replace(result, "%union_fields_query%",
                          StringUtil::Join(tokenize_fields, " UNION ALL "));
  result = StringUtil::Replace(result, "%token_ctes%", token_ctes);
  result = StringUtil::Replace(result, "%field_length_aggregates%",
                               FieldLengthAggregateList(input_values.size()));
  result =
      StringUtil::Replace(result, "%zero_field_lengths%",
                          ZeroFieldLengthList(input_values.size(), "BIGINT"));
  result =
      StringUtil::Replace(result, "%stats_delta_update%",
                          StatsDeltaUpdateStatement(input_values.size(), true));
  result = StringUtil::Replace(
      result, "%layered_insert_new_terms_triggers%",
      layered_search ? LayeredInsertNewTermsTriggerScript(qname, token_ctes)
                     : "");
  result = StringUtil::Replace(
      result, "%layered_insert_df_triggers%",
      layered_search ? LayeredInsertDFTriggerScript(qname, token_ctes, input_id)
                     : "");
  result = StringUtil::Replace(
      result, "%insert_terms_order_by%",
      layered_search ? "ORDER BY d.termid, rd.rawtermid, ss.fieldid, ss.docid"
                     : "");
  result =
      StringUtil::Replace(result, "%terms_insert_columns%",
                          layered_search ? "docid, fieldid, termid, rawtermid"
                                         : "docid, fieldid, termid");
  result = StringUtil::Replace(result, "%rawtermid_select%",
                               layered_search ? ", rd.rawtermid" : "");
  result = StringUtil::Replace(
      result, "%raw_dict_join%",
      layered_search ? "JOIN %fts_schema%.raw_dict AS rd ON rd.raw_term = "
                       "ss.raw_term AND rd.termid = d.termid"
                     : "");

  auto trigger_names = GetFTSInsertTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_00_docs%",
                               SQLIdentifier::ToString(trigger_names[0]));
  result = StringUtil::Replace(result, "%trigger_10_dict_insert%",
                               SQLIdentifier::ToString(trigger_names[1]));
  result = StringUtil::Replace(result, "%trigger_20_terms%",
                               SQLIdentifier::ToString(trigger_names[2]));
  result = StringUtil::Replace(result, "%trigger_30_dict_df%",
                               SQLIdentifier::ToString(trigger_names[3]));
  result = StringUtil::Replace(result, "%trigger_40_stats%",
                               SQLIdentifier::ToString(trigger_names[4]));
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  return result;
}

static string ClusteredInsertTriggerScript(const QualifiedName &qname,
                                           const string &input_id,
                                           const vector<string> &input_values) {
  // clang-format off
	string docs_new_docs_cte = R"(
        fts_new_docs AS (
            SELECT COALESCE((SELECT max(docid) + 1 FROM %fts_schema%.docs), 0)
                       + row_number() OVER (ORDER BY fts_new_rows.%input_id%) - 1 AS docid,
                   fts_new_rows.%input_id% AS name,
                   %input_value_select_list%
            FROM fts_new_rows
        )
    )";

	string token_new_docs_cte = R"(
        fts_new_docs AS (
            SELECT (SELECT max(docid) - (SELECT count(*) FROM fts_new_rows) + 1 FROM %fts_schema%.docs)
                       + row_number() OVER (ORDER BY fts_new_rows.%input_id%) - 1 AS docid,
                   fts_new_rows.%input_id% AS name,
                   %input_value_select_list%
            FROM fts_new_rows
        )
    )";

	string tokenize_field_query = R"(
        SELECT unnest(%fts_schema%.tokenize(fts_ii.%input_value%)) AS w,
               fts_ii.docid AS docid,
               (SELECT fieldid FROM %fts_schema%.fields WHERE field = %input_value_string%) AS fieldid
        FROM fts_new_docs AS fts_ii
    )";

	string token_ctes = R"(
        WITH %new_docs_cte%,
        tokenized AS (
            %union_fields_query%
        ),
        stemmed_stopped AS (
            SELECT stem(t.w, '%stemmer%') AS term,
                   t.docid AS docid,
                   t.fieldid AS fieldid
            FROM tokenized AS t
            WHERE t.w NOT NULL
              AND t.w <> ''
              AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)
        )
    )";

	string result = R"(
        CREATE TRIGGER %trigger_00_docs% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.docs (docid, name, len, field_lens)
            WITH %docs_new_docs_cte%,
            tokenized AS (
                %union_fields_query%
            ),
            stemmed_stopped AS (
                SELECT stem(t.w, '%stemmer%') AS term,
                       t.docid AS docid,
                       t.fieldid AS fieldid
                FROM tokenized AS t
                WHERE t.w NOT NULL
                  AND t.w <> ''
                  AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)
            ),
            lengths AS (
                SELECT docid,
                       count(*)::BIGINT AS len,
                       %field_length_aggregates% AS field_lens
                FROM stemmed_stopped
                GROUP BY docid
            )
            SELECT nd.docid,
                   nd.name,
                   coalesce(lengths.len, 0)::BIGINT AS len,
                   coalesce(lengths.field_lens, %zero_field_lengths%)::BIGINT[] AS field_lens
            FROM fts_new_docs AS nd
            LEFT JOIN lengths ON lengths.docid = nd.docid;

        CREATE TRIGGER %trigger_10_dict_insert% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.dict (termid, term, df)
            %token_ctes%,
            new_terms AS (
                SELECT DISTINCT term
                FROM stemmed_stopped
                WHERE term NOT IN (SELECT term FROM %fts_schema%.dict)
                ORDER BY term
            )
            SELECT (SELECT COALESCE(max(termid) + 1, 0) FROM %fts_schema%.dict) + row_number() OVER () - 1 AS termid,
                   term,
                   0 AS df
            FROM new_terms;

        CREATE TRIGGER %trigger_20_terms_store% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.%terms_storage_table% (termid, docid, fieldid)
            %token_ctes%
            SELECT d.termid,
                   ss.docid,
                   ss.fieldid
            FROM stemmed_stopped AS ss
            JOIN %fts_schema%.dict AS d ON ss.term = d.term;

        CREATE TRIGGER %trigger_30_dict_df% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.dict AS d
            SET df = d.df + inserted_df_delta.df_delta
            FROM (
                SELECT t.termid,
                       COUNT(DISTINCT t.docid) AS df_delta
                FROM %fts_schema%.%terms_storage_table% AS t
                JOIN %fts_schema%.docs AS docs ON t.docid = docs.docid
                JOIN fts_new_rows AS new_rows ON docs.name = new_rows.%input_id%
                GROUP BY t.termid
            ) AS inserted_df_delta
            WHERE d.termid = inserted_df_delta.termid;

        CREATE TRIGGER %trigger_40_stats% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            %stats_delta_update%;
    )";
  // clang-format on

  vector<string> input_value_selects;
  vector<string> tokenize_fields;
  for (auto &input_value : input_values) {
    input_value_selects.push_back(StringUtil::Format(
        "fts_new_rows.%s AS %s", SQLIdentifier::ToString(input_value),
        SQLIdentifier::ToString(input_value)));
    auto query = StringUtil::Replace(tokenize_field_query, "%input_value%",
                                     SQLIdentifier::ToString(input_value));
    query = StringUtil::Replace(query, "%input_value_string%",
                                SQLString::ToString(input_value));
    tokenize_fields.push_back(query);
  }

  docs_new_docs_cte = StringUtil::Replace(
      docs_new_docs_cte, "%input_value_select_list%",
      StringUtil::Join(input_value_selects, ",\n                   "));
  token_new_docs_cte = StringUtil::Replace(
      token_new_docs_cte, "%input_value_select_list%",
      StringUtil::Join(input_value_selects, ",\n                   "));
  token_ctes =
      StringUtil::Replace(token_ctes, "%new_docs_cte%", token_new_docs_cte);
  token_ctes =
      StringUtil::Replace(token_ctes, "%union_fields_query%",
                          StringUtil::Join(tokenize_fields, " UNION ALL "));
  result =
      StringUtil::Replace(result, "%docs_new_docs_cte%", docs_new_docs_cte);
  result =
      StringUtil::Replace(result, "%union_fields_query%",
                          StringUtil::Join(tokenize_fields, " UNION ALL "));
  result = StringUtil::Replace(result, "%token_ctes%", token_ctes);
  result = StringUtil::Replace(result, "%field_length_aggregates%",
                               FieldLengthAggregateList(input_values.size()));
  result =
      StringUtil::Replace(result, "%zero_field_lengths%",
                          ZeroFieldLengthList(input_values.size(), "BIGINT"));
  result =
      StringUtil::Replace(result, "%stats_delta_update%",
                          StatsDeltaUpdateStatement(input_values.size(), true));
  result = StringUtil::Replace(result, "%terms_storage_table%",
                               GetFTSTermsStorageTable());

  auto trigger_names = GetFTSClusteredInsertTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_00_docs%",
                               SQLIdentifier::ToString(trigger_names[0]));
  result = StringUtil::Replace(result, "%trigger_10_dict_insert%",
                               SQLIdentifier::ToString(trigger_names[1]));
  result = StringUtil::Replace(result, "%trigger_20_terms_store%",
                               SQLIdentifier::ToString(trigger_names[2]));
  result = StringUtil::Replace(result, "%trigger_30_dict_df%",
                               SQLIdentifier::ToString(trigger_names[3]));
  result = StringUtil::Replace(result, "%trigger_40_stats%",
                               SQLIdentifier::ToString(trigger_names[4]));
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  return result;
}

static string DeleteTriggerScript(const QualifiedName &qname,
                                  const string &input_id,
                                  const vector<string> &input_values,
                                  bool layered_search) {
  // clang-format off
	string tokenize_field_query = R"(
        SELECT unnest(%fts_schema%.tokenize(fts_di.%input_value%)) AS w
        FROM fts_old_rows AS fts_di
    )";

	string token_ctes = R"(
        WITH tokenized AS (
            %union_fields_query%
        ),
        stemmed_stopped AS (
            SELECT t.w AS raw_term,
                   stem(t.w, '%stemmer%') AS term
            FROM tokenized AS t
            WHERE t.w NOT NULL
              AND t.w <> ''
              AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)
        )
    )";

	string result = R"(
        CREATE TRIGGER %trigger_00_dict_df% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.dict AS d
            SET df = d.df - deleted_df_delta.df_delta
            FROM (
                SELECT t.termid,
                       COUNT(DISTINCT t.docid) AS df_delta
                FROM %fts_schema%.terms AS t
                JOIN %fts_schema%.docs AS docs ON t.docid = docs.docid
                JOIN fts_old_rows AS old_rows ON docs.name = old_rows.%input_id%
                GROUP BY t.termid
            ) AS deleted_df_delta
            WHERE d.termid = deleted_df_delta.termid;

        %layered_delete_before_terms_triggers%

        CREATE TRIGGER %trigger_10_terms% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.terms
            WHERE docid IN (
                SELECT d.docid
                FROM %fts_schema%.docs AS d
                JOIN fts_old_rows AS old_rows ON d.name = old_rows.%input_id%
            );

        CREATE TRIGGER %trigger_15_stats% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            %stats_delta_update%;

        CREATE TRIGGER %trigger_20_docs% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.docs
            WHERE name IN (SELECT %input_id% FROM fts_old_rows);

        %layered_delete_after_df_triggers%

        CREATE TRIGGER %trigger_30_dict_prune% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.dict
            WHERE df = 0;

  )";
  // clang-format on

  vector<string> tokenize_fields;
  for (auto &input_value : input_values) {
    auto query = StringUtil::Replace(tokenize_field_query, "%input_value%",
                                     SQLIdentifier::ToString(input_value));
    tokenize_fields.push_back(query);
  }
  token_ctes =
      StringUtil::Replace(token_ctes, "%union_fields_query%",
                          StringUtil::Join(tokenize_fields, " UNION ALL "));

  auto trigger_names = GetFTSDeleteTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_00_dict_df%",
                               SQLIdentifier::ToString(trigger_names[0]));
  result = StringUtil::Replace(result, "%trigger_10_terms%",
                               SQLIdentifier::ToString(trigger_names[1]));
  result = StringUtil::Replace(result, "%trigger_15_stats%",
                               SQLIdentifier::ToString(trigger_names[2]));
  result = StringUtil::Replace(result, "%trigger_20_docs%",
                               SQLIdentifier::ToString(trigger_names[3]));
  result = StringUtil::Replace(result, "%trigger_30_dict_prune%",
                               SQLIdentifier::ToString(trigger_names[4]));
  result = StringUtil::Replace(
      result, "%layered_delete_before_terms_triggers%",
      layered_search ? LayeredDeleteBeforeTermsTriggerScript(qname, input_id)
                     : "");
  result = StringUtil::Replace(
      result, "%layered_delete_after_df_triggers%",
      layered_search ? LayeredDeleteAfterDFTriggerScript(qname, token_ctes)
                     : "");
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(
      result, "%stats_delta_update%",
      StatsDeltaUpdateStatement(input_values.size(), false));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  return result;
}

static string ClusteredDeleteTriggerScript(const QualifiedName &qname,
                                           const string &input_id,
                                           idx_t field_count) {
  // clang-format off
	string result = R"(
        CREATE TRIGGER %trigger_00_dict_df% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.dict AS d
            SET df = d.df - deleted_df_delta.df_delta
            FROM (
                SELECT t.termid,
                       COUNT(DISTINCT t.docid) AS df_delta
                FROM %fts_schema%.%terms_storage_table% AS t
                JOIN %fts_schema%.docs AS docs ON t.docid = docs.docid
                JOIN fts_old_rows AS old_rows ON docs.name = old_rows.%input_id%
                GROUP BY t.termid
            ) AS deleted_df_delta
            WHERE d.termid = deleted_df_delta.termid;

        CREATE TRIGGER %trigger_10_terms_store% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.%terms_storage_table%
            WHERE docid IN (
                SELECT d.docid
                FROM %fts_schema%.docs AS d
                JOIN fts_old_rows AS old_rows ON d.name = old_rows.%input_id%
            );

        CREATE TRIGGER %trigger_15_stats% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            %stats_delta_update%;

        CREATE TRIGGER %trigger_20_docs% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.docs
            WHERE name IN (SELECT %input_id% FROM fts_old_rows);

        CREATE TRIGGER %trigger_30_dict_prune% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            DELETE FROM %fts_schema%.dict
            WHERE df = 0;

    )";
  // clang-format on

  auto trigger_names = GetFTSClusteredDeleteTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_00_dict_df%",
                               SQLIdentifier::ToString(trigger_names[0]));
  result = StringUtil::Replace(result, "%trigger_10_terms_store%",
                               SQLIdentifier::ToString(trigger_names[1]));
  result = StringUtil::Replace(result, "%trigger_15_stats%",
                               SQLIdentifier::ToString(trigger_names[2]));
  result = StringUtil::Replace(result, "%trigger_20_docs%",
                               SQLIdentifier::ToString(trigger_names[3]));
  result = StringUtil::Replace(result, "%trigger_30_dict_prune%",
                               SQLIdentifier::ToString(trigger_names[4]));
  result = StringUtil::Replace(result, "%terms_storage_table%",
                               GetFTSTermsStorageTable());
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(result, "%stats_delta_update%",
                               StatsDeltaUpdateStatement(field_count, false));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  return result;
}

// ---------------------------------------------------------------------------
// Coordinator: assembles all parts and substitutes cross-cutting placeholders
// ---------------------------------------------------------------------------

static string IndexingScript(ClientContext &context, QualifiedName &qname,
                             const string &input_id,
                             const vector<string> &input_values,
                             const string &stemmer, const string &stopwords,
                             const string &tokenizer, const string &ignore,
                             bool strip_accents, bool lower, bool incremental,
                             bool cluster_terms, bool layered_search,
                             bool structured_queries) {
  string result;
  if (TableExists(context, qname) && SupportsFTSTriggers(context, qname)) {
    result += DropFTSTriggersScript(qname);
  }
  result += SchemaSetupScript(qname);
  result += StopwordsScript(context, qname, stopwords);
  result += TokenizeMacroScript(qname, tokenizer, ignore, strip_accents, lower);
  result += IndexTablesScript(
      qname, input_id, input_values, stemmer, stopwords,
      GetFTSBuildTermsTable(qname), GetFTSBuildDictTable(qname),
      GetFTSBuildRawDictTable(qname), cluster_terms, layered_search);
  result += MatchMacroScript(qname, stemmer);
  if (layered_search) {
    result += LayeredSidecarScript(qname);
    result += LayeredSearchMacroScript(qname, stemmer, structured_queries);
  }
  if (incremental) {
    result += IncrementalIndexSetupScript(qname);
    if (layered_search) {
      result +=
          InsertTriggerScript(qname, input_id, input_values, layered_search);
      result +=
          DeleteTriggerScript(qname, input_id, input_values, layered_search);
    } else if (cluster_terms) {
      result += ClusteredIncrementalIndexSetupScript();
      result += ClusteredInsertTriggerScript(qname, input_id, input_values);
      result +=
          ClusteredDeleteTriggerScript(qname, input_id, input_values.size());
    } else {
      result +=
          InsertTriggerScript(qname, input_id, input_values, layered_search);
      result +=
          DeleteTriggerScript(qname, input_id, input_values, layered_search);
    }
  }

  string fts_schema = GetFTSSchema(qname);
  string input_table = GetQualifiedTableName(qname);

  result = StringUtil::Replace(result, "%fts_schema%", fts_schema);
  result = StringUtil::Replace(result, "%input_table%", input_table);
  result = StringUtil::Replace(result, "%stemmer%", stemmer);
  return result;
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
    result += DropFTSTriggersScript(qname);
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

  if (stopwords != "english" && stopwords != "none") {
    auto sw_qname = GetQualifiedName(context, stopwords);
    Catalog::GetEntry<TableCatalogEntry>(context, sw_qname);
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
  return IndexingScript(context, qname, doc_id, doc_values, stemmer, stopwords,
                        tokenizer, ignore, strip_accents, lower, incremental,
                        cluster_terms, layered_search, structured_queries);
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

  return StructuredLayeredSearchMacroScript(qname);
}

} // namespace duckdb
