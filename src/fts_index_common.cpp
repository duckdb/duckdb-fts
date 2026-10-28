#include "fts_index_common.hpp"

#include "fts_sql_assets.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

string GetFTSSchemaName(const QualifiedName &qname) {
  return StringUtil::Format("fts_%s_%s", qname.Schema().GetIdentifierName(),
                            qname.Name().GetIdentifierName());
}

string GetFTSSchema(const QualifiedName &qname) {
  auto result =
      IsInvalidCatalog(qname.Catalog())
          ? string("")
          : SQLIdentifier::ToString(qname.Catalog().GetIdentifierName()) + ".";
  result += SQLIdentifier::ToString(GetFTSSchemaName(qname));
  return result;
}

SQLTemplateArgument GetFTSSchemaArgument(const QualifiedName &qname) {
  vector<string> parts;
  if (!IsInvalidCatalog(qname.Catalog())) {
    parts.push_back(qname.Catalog().GetIdentifierName());
  }
  parts.push_back(GetFTSSchemaName(qname));
  return SQLTemplateArgument::QualifiedIdentifier(parts);
}

string GetQualifiedTableName(const QualifiedName &qname) {
  return GetQualifiedTableArgument(qname).GetSQL();
}

SQLTemplateArgument GetQualifiedTableArgument(const QualifiedName &qname) {
  vector<string> parts;
  if (!IsInvalidCatalog(qname.Catalog())) {
    parts.push_back(qname.Catalog().GetIdentifierName());
  }
  parts.push_back(qname.Schema().GetIdentifierName());
  parts.push_back(qname.Name().GetIdentifierName());
  return SQLTemplateArgument::QualifiedIdentifier(parts);
}

static string
AnalyzeTokenStreamProjectionSQL(AnalyzeTokenStreamProjection projection) {
  switch (projection) {
  case AnalyzeTokenStreamProjection::NONE:
    return "";
  case AnalyzeTokenStreamProjection::POSITION:
    return ",\n       tokenized.position";
  case AnalyzeTokenStreamProjection::POSITION_OFFSETS:
    return ",\n       tokenized.position,\n       tokenized.start_offset,\n"
           "       tokenized.end_offset";
  case AnalyzeTokenStreamProjection::DOCID_FIELDID:
    return ",\n       tokenized.docid,\n       tokenized.fieldid";
  case AnalyzeTokenStreamProjection::DOCID_FIELDID_POSITION:
    return ",\n       tokenized.docid,\n       tokenized.fieldid,\n       "
           "tokenized.position";
  case AnalyzeTokenStreamProjection::TOKEN_POSITION:
    return ",\n       tokenized.token_position";
  }
  throw InternalException("Unsupported analyzer token stream projection");
}

string RenderAnalyzeTokenStream(const QualifiedName &qname,
                                const FTSAnalyzerConfig &config,
                                AnalyzeTokenStreamProjection projection) {
  auto term_expression = config.stemmer == "none"
                             ? "tokenized.raw_term"
                             : "stem(tokenized.raw_term, " +
                                   SQLString::ToString(config.stemmer) + ")";
  auto stopwords_filter = config.filter_stopwords
                              ? "  AND tokenized.raw_term NOT IN (\n"
                                "      SELECT sw\n"
                                "      FROM " +
                                    GetFTSSchema(qname) +
                                    ".stopwords\n"
                                    "  )"
                              : "";
  return RenderSQLTemplate(
      fts_sql::ANALYZE_TOKEN_STREAM,
      {{"term_expression", SQLTemplateArgument::TrustedSQL(term_expression)},
       {"projected_columns", SQLTemplateArgument::TrustedSQL(
                                 AnalyzeTokenStreamProjectionSQL(projection))},
       {"stopwords_filter",
        SQLTemplateArgument::TrustedSQL(stopwords_filter)}});
}

string RenderQueryAnalysis(const QualifiedName &qname,
                           const FTSAnalyzerConfig &config) {
  auto trailing_stopword =
      config.filter_stopwords
          ? RenderSQLTemplate(fts_sql::QUERY_TRAILING_STOPWORD,
                              {{"fts_schema", GetFTSSchemaArgument(qname)}})
          : "";
  return RenderSQLTemplate(
      fts_sql::QUERY_ANALYSIS_CTES,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, config, AnalyzeTokenStreamProjection::TOKEN_POSITION))},
       {"trailing_stopword_token",
        SQLTemplateArgument::TrustedSQL(trailing_stopword)}});
}

static string GetFTSTriggerPrefix(const QualifiedName &qname,
                                  const string &infix) {
  return StringUtil::Format("__fts_%s_%s_", GetFTSSchemaName(qname), infix);
}

vector<string> GetFTSInsertTriggerNames(const QualifiedName &qname,
                                        const string &infix) {
  auto prefix = GetFTSTriggerPrefix(qname, infix);
  return {prefix + "00_docs", prefix + "10_dict_insert", prefix + "20_terms",
          prefix + "30_dict_df", prefix + "40_stats"};
}

vector<string> GetFTSClusteredInsertTriggerNames(const QualifiedName &qname,
                                                 const string &infix) {
  auto prefix = GetFTSTriggerPrefix(qname, infix);
  return {prefix + "00_docs", prefix + "10_dict_insert",
          prefix + "20_terms_store", prefix + "30_dict_df",
          prefix + "40_stats"};
}

vector<string> GetFTSDeleteTriggerNames(const QualifiedName &qname,
                                        const string &infix) {
  auto prefix = GetFTSTriggerPrefix(qname, infix);
  return {prefix + "00_dict_df", prefix + "10_terms", prefix + "15_stats",
          prefix + "20_docs", prefix + "30_dict_prune"};
}

vector<string> GetFTSClusteredDeleteTriggerNames(const QualifiedName &qname,
                                                 const string &infix) {
  auto prefix = GetFTSTriggerPrefix(qname, infix);
  return {prefix + "00_dict_df", prefix + "10_terms_store", prefix + "15_stats",
          prefix + "20_docs", prefix + "30_dict_prune"};
}

vector<string> GetFTSLayeredInsertTriggerNames(const QualifiedName &qname,
                                               const string &infix) {
  auto prefix = GetFTSTriggerPrefix(qname, infix);
  return {prefix + "15_term_stats",    prefix + "16_term_stats_by_len",
          prefix + "17_term_grams",    prefix + "18_raw_dict",
          prefix + "19_term_prefixes", prefix + "19_raw_term_grams",
          prefix + "35_term_stats_df", prefix + "36_term_stats_by_len_df",
          prefix + "37_raw_dict_df"};
}

vector<string> GetFTSLayeredDeleteTriggerNames(const QualifiedName &qname,
                                               const string &infix) {
  auto prefix = GetFTSTriggerPrefix(qname, infix);
  return {
      prefix + "05_raw_dict_df",          prefix + "21_term_prefixes_prune",
      prefix + "21_raw_term_grams_prune", prefix + "22_raw_dict_prune",
      prefix + "23_term_stats_df",        prefix + "24_term_stats_by_len_df",
      prefix + "25_term_grams_prune",     prefix + "26_term_stats_by_len_prune",
      prefix + "27_term_stats_prune"};
}

vector<string> GetFTSTriggerNames(const QualifiedName &qname) {
  vector<string> result;
  auto append = [&](vector<string> names) {
    result.insert(result.end(), names.begin(), names.end());
  };
  for (const string insert_infix : {"ai", FTS_UPDATE_INSERT_TRIGGER_INFIX}) {
    append(GetFTSInsertTriggerNames(qname, insert_infix));
    append(GetFTSLayeredInsertTriggerNames(qname, insert_infix));
    append(GetFTSClusteredInsertTriggerNames(qname, insert_infix));
  }
  for (const string delete_infix : {"ad", FTS_UPDATE_DELETE_TRIGGER_INFIX}) {
    append(GetFTSDeleteTriggerNames(qname, delete_infix));
    append(GetFTSClusteredDeleteTriggerNames(qname, delete_infix));
    append(GetFTSLayeredDeleteTriggerNames(qname, delete_infix));
  }
  // Indexes created before delta statistics used this delete trigger name.
  result.push_back(GetFTSTriggerPrefix(qname, "ad") + "40_stats");
  return result;
}

string GetFTSBuildTermsTable(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_build_terms_" +
                                 GetFTSSchemaName(qname));
}

string GetFTSBuildDictTable(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_build_dict_" + GetFTSSchemaName(qname));
}

string GetFTSTermsStorageTable() {
  return SQLIdentifier::ToString("__terms_storage");
}

string GetFTSBuildRawDictTable(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_build_raw_dict_" +
                                 GetFTSSchemaName(qname));
}

string GetFTSTermStatsTermIndex(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_" + GetFTSSchemaName(qname) +
                                 "_term_stats_term_idx");
}

string GetFTSTermGramsGramIndex(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_" + GetFTSSchemaName(qname) +
                                 "_term_grams_gram_idx");
}

string GetFTSTermPrefixesPrefixIndex(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_" + GetFTSSchemaName(qname) +
                                 "_term_prefixes_prefix_idx");
}

string GetFTSRawTermGramsGramIndex(const QualifiedName &qname) {
  return SQLIdentifier::ToString("__fts_" + GetFTSSchemaName(qname) +
                                 "_raw_term_grams_gram_idx");
}

string FieldLengthAggregateList(idx_t field_count) {
  vector<string> field_lengths;
  for (idx_t field_id = 0; field_id < field_count; field_id++) {
    field_lengths.push_back(StringUtil::Format(
        "count(*) FILTER (WHERE fieldid = %i)::BIGINT", field_id));
  }
  return "list_value(" + StringUtil::Join(field_lengths, ", ") + ")";
}

string ZeroFieldLengthList(idx_t field_count, const string &type) {
  vector<string> field_lengths;
  for (idx_t field_id = 0; field_id < field_count; field_id++) {
    field_lengths.push_back("0::" + type);
  }
  return "list_value(" + StringUtil::Join(field_lengths, ", ") + ")";
}

string StatsFieldAggregateList(idx_t field_count, const string &aggregate,
                               const string &fallback, const string &type) {
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

} // namespace duckdb
