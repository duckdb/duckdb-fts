#include "fts_index_common.hpp"

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

vector<string> GetFTSInsertTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ai_", GetFTSSchemaName(qname));
  return {prefix + "00_docs", prefix + "10_dict_insert", prefix + "20_terms",
          prefix + "30_dict_df", prefix + "40_stats"};
}

vector<string> GetFTSClusteredInsertTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ai_", GetFTSSchemaName(qname));
  return {prefix + "00_docs", prefix + "10_dict_insert",
          prefix + "20_terms_store", prefix + "30_dict_df",
          prefix + "40_stats"};
}

vector<string> GetFTSDeleteTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ad_", GetFTSSchemaName(qname));
  return {prefix + "00_dict_df", prefix + "10_terms", prefix + "15_stats",
          prefix + "20_docs", prefix + "30_dict_prune"};
}

vector<string> GetFTSClusteredDeleteTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ad_", GetFTSSchemaName(qname));
  return {prefix + "00_dict_df", prefix + "10_terms_store", prefix + "15_stats",
          prefix + "20_docs", prefix + "30_dict_prune"};
}

vector<string> GetFTSLayeredInsertTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ai_", GetFTSSchemaName(qname));
  return {prefix + "15_term_stats",           prefix + "16_term_stats_by_len",
          prefix + "17_term_grams",           prefix + "18_raw_dict",
          prefix + "19_term_prefixes",        prefix + "35_term_stats_df",
          prefix + "36_term_stats_by_len_df", prefix + "37_raw_dict_df"};
}

vector<string> GetFTSLayeredDeleteTriggerNames(const QualifiedName &qname) {
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

vector<string> GetFTSTriggerNames(const QualifiedName &qname) {
  auto result = GetFTSInsertTriggerNames(qname);
  auto append = [&](vector<string> names) {
    result.insert(result.end(), names.begin(), names.end());
  };
  append(GetFTSLayeredInsertTriggerNames(qname));
  append(GetFTSDeleteTriggerNames(qname));
  append(GetFTSClusteredInsertTriggerNames(qname));
  append(GetFTSClusteredDeleteTriggerNames(qname));
  append(GetFTSLayeredDeleteTriggerNames(qname));
  // Indexes created before delta statistics used this delete trigger name.
  auto delete_prefix =
      StringUtil::Format("__fts_%s_ad_", GetFTSSchemaName(qname));
  result.push_back(delete_prefix + "40_stats");
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
