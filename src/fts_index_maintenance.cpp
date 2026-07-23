#include "fts_index_maintenance.hpp"

#include "fts_index_common.hpp"
#include "fts_sql_assets.hpp"
#include "fts_sql_template.hpp"

#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

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

static string StatsDeltaUpdateStatement(const QualifiedName &qname,
                                        const string &input_id,
                                        idx_t field_count, bool insert) {
  auto operation = insert ? "+" : "-";
  auto transition_table = insert ? "fts_new_rows" : "fts_old_rows";
  vector<string> delta_field_lengths;
  for (idx_t field_id = 0; field_id < field_count; field_id++) {
    delta_field_lengths.push_back(StringUtil::Format(
        "coalesce(sum(list_extract(docs.field_lens, %i)), 0)::HUGEINT AS "
        "field_len_%i",
        field_id + 1, field_id));
  }

  return RenderSQLTemplate(
      fts_sql::STATS_DELTA_UPDATE,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"operation", SQLTemplateArgument::TrustedSQL(operation)},
       {"average_field_lengths",
        SQLTemplateArgument::TrustedSQL(
            StatsDeltaFieldList(field_count, operation, true))},
       {"total_field_lengths",
        SQLTemplateArgument::TrustedSQL(
            StatsDeltaFieldList(field_count, operation, false))},
       {"delta_field_lengths",
        SQLTemplateArgument::TrustedSQL(StringUtil::Join(
            delta_field_lengths, ",\n                       "))},
       {"transition_table", SQLTemplateArgument::Identifier(transition_table)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)}});
}

string FTSIndexMaintenance::DropTriggers(const QualifiedName &qname) {
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
  return RenderSQLTemplate(
      fts_sql::INCREMENTAL_INDEX_SETUP,
      {{"index_name", SQLTemplateArgument::Identifier(index_name)},
       {"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string ClusteredIncrementalIndexSetupScript(const QualifiedName &qname) {
  return RenderSQLTemplate(
      fts_sql::CLUSTERED_INCREMENTAL_INDEX_SETUP,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"terms_storage_table",
        SQLTemplateArgument::TrustedSQL(GetFTSTermsStorageTable())}});
}

static string LayeredAffectedTermIdsSubquery(const QualifiedName &qname,
                                             const string &token_ctes) {
  return token_ctes +
         RenderSQLTemplate(fts_sql::LAYERED_AFFECTED_TERM_IDS,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string LayeredInsertNewTermsTriggerScript(const QualifiedName &qname,
                                                 const string &token_ctes) {
  auto trigger_names = GetFTSLayeredInsertTriggerNames(qname);
  return RenderSQLTemplate(
      fts_sql::LAYERED_INSERT_NEW_TERMS,
      {{"trigger_15_term_stats",
        SQLTemplateArgument::Identifier(trigger_names[0])},
       {"trigger_16_term_stats_by_len",
        SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_17_term_grams",
        SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_18_raw_dict",
        SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_19_term_prefixes",
        SQLTemplateArgument::Identifier(trigger_names[4])},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"fts_schema", GetFTSSchemaArgument(qname)},
       {"token_ctes", SQLTemplateArgument::TrustedSQL(token_ctes)}});
}

static string LayeredInsertDFTriggerScript(const QualifiedName &qname,
                                           const string &token_ctes,
                                           const string &input_id) {
  auto trigger_names = GetFTSLayeredInsertTriggerNames(qname);
  auto affected_termids = LayeredAffectedTermIdsSubquery(qname, token_ctes);
  return RenderSQLTemplate(
      fts_sql::LAYERED_INSERT_DF,
      {{"trigger_35_term_stats_df",
        SQLTemplateArgument::Identifier(trigger_names[5])},
       {"trigger_36_term_stats_by_len_df",
        SQLTemplateArgument::Identifier(trigger_names[6])},
       {"trigger_37_raw_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[7])},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"fts_schema", GetFTSSchemaArgument(qname)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"affected_termids",
        SQLTemplateArgument::TrustedSQL(affected_termids)}});
}

static string LayeredDeleteBeforeTermsTriggerScript(const QualifiedName &qname,
                                                    const string &input_id) {
  auto trigger_names = GetFTSLayeredDeleteTriggerNames(qname);
  return RenderSQLTemplate(
      fts_sql::LAYERED_DELETE_BEFORE_TERMS,
      {{"trigger_05_raw_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[0])},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"fts_schema", GetFTSSchemaArgument(qname)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)}});
}

static string LayeredDeleteAfterDFTriggerScript(const QualifiedName &qname,
                                                const string &token_ctes) {
  auto trigger_names = GetFTSLayeredDeleteTriggerNames(qname);
  auto affected_termids = LayeredAffectedTermIdsSubquery(qname, token_ctes);
  return RenderSQLTemplate(
      fts_sql::LAYERED_DELETE_AFTER_DF,
      {{"trigger_21_term_prefixes_prune",
        SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_22_raw_dict_prune",
        SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_23_term_stats_df",
        SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_24_term_stats_by_len_df",
        SQLTemplateArgument::Identifier(trigger_names[4])},
       {"trigger_25_term_grams_prune",
        SQLTemplateArgument::Identifier(trigger_names[5])},
       {"trigger_26_term_stats_by_len_prune",
        SQLTemplateArgument::Identifier(trigger_names[6])},
       {"trigger_27_term_stats_prune",
        SQLTemplateArgument::Identifier(trigger_names[7])},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"fts_schema", GetFTSSchemaArgument(qname)},
       {"affected_termids",
        SQLTemplateArgument::TrustedSQL(affected_termids)}});
}

static vector<string>
BuildInsertedFieldQueries(const QualifiedName &qname,
                          const vector<string> &input_values,
                          vector<string> &input_value_selects) {
  vector<string> tokenize_fields;
  for (auto &input_value : input_values) {
    input_value_selects.push_back(StringUtil::Format(
        "fts_new_rows.%s AS %s", SQLIdentifier::ToString(input_value),
        SQLIdentifier::ToString(input_value)));
    tokenize_fields.push_back(RenderSQLTemplate(
        fts_sql::TOKENIZE_INSERTED_FIELD,
        {{"fts_schema", GetFTSSchemaArgument(qname)},
         {"input_value", SQLTemplateArgument::Identifier(input_value)},
         {"input_value_string",
          SQLTemplateArgument::StringLiteral(input_value)}}));
  }
  return tokenize_fields;
}

static string InsertTriggerScript(const QualifiedName &qname,
                                  const string &input_id,
                                  const vector<string> &input_values,
                                  const string &stemmer, bool layered_search) {
  vector<string> input_value_selects;
  auto tokenize_fields =
      BuildInsertedFieldQueries(qname, input_values, input_value_selects);
  auto input_value_select_list =
      StringUtil::Join(input_value_selects, ",\n                   ");
  auto union_fields_query = StringUtil::Join(tokenize_fields, " UNION ALL ");
  auto docs_new_docs_cte = RenderSQLTemplate(
      fts_sql::INSERT_DOCS_CTE,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"input_value_select_list",
        SQLTemplateArgument::TrustedSQL(input_value_select_list)}});
  auto token_new_docs_cte = RenderSQLTemplate(
      fts_sql::INSERT_TOKEN_DOCS_CTE,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"input_value_select_list",
        SQLTemplateArgument::TrustedSQL(input_value_select_list)}});
  auto token_ctes = RenderSQLTemplate(
      fts_sql::LAYERED_INSERT_TOKEN_CTES,
      {{"new_docs_cte", SQLTemplateArgument::TrustedSQL(token_new_docs_cte)},
       {"union_fields_query",
        SQLTemplateArgument::TrustedSQL(union_fields_query)},
       {"stemmer", SQLTemplateArgument::StringLiteral(stemmer)},
       {"fts_schema", GetFTSSchemaArgument(qname)}});
  auto trigger_names = GetFTSInsertTriggerNames(qname);
  auto raw_dict_join =
      layered_search ? "JOIN " + GetFTSSchema(qname) +
                           ".raw_dict AS rd ON rd.raw_term = ss.raw_term AND "
                           "rd.termid = d.termid"
                     : "";
  return RenderSQLTemplate(
      fts_sql::INSERT_TRIGGERS,
      {{"trigger_00_docs", SQLTemplateArgument::Identifier(trigger_names[0])},
       {"trigger_10_dict_insert",
        SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_20_terms", SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_30_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_40_stats", SQLTemplateArgument::Identifier(trigger_names[4])},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"fts_schema", GetFTSSchemaArgument(qname)},
       {"docs_new_docs_cte",
        SQLTemplateArgument::TrustedSQL(docs_new_docs_cte)},
       {"union_fields_query",
        SQLTemplateArgument::TrustedSQL(union_fields_query)},
       {"stemmer", SQLTemplateArgument::StringLiteral(stemmer)},
       {"field_length_aggregates",
        SQLTemplateArgument::TrustedSQL(
            FieldLengthAggregateList(input_values.size()))},
       {"zero_field_lengths",
        SQLTemplateArgument::TrustedSQL(
            ZeroFieldLengthList(input_values.size(), "BIGINT"))},
       {"token_ctes", SQLTemplateArgument::TrustedSQL(token_ctes)},
       {"layered_insert_new_terms_triggers",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? LayeredInsertNewTermsTriggerScript(qname, token_ctes)
                : "")},
       {"terms_insert_columns",
        SQLTemplateArgument::TrustedSQL(
            layered_search ? "docid, fieldid, termid, rawtermid"
                           : "docid, fieldid, termid")},
       {"rawtermid_select", SQLTemplateArgument::TrustedSQL(
                                layered_search ? ", rd.rawtermid" : "")},
       {"raw_dict_join", SQLTemplateArgument::TrustedSQL(raw_dict_join)},
       {"insert_terms_order_by",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? "ORDER BY d.termid, rd.rawtermid, ss.fieldid, ss.docid"
                : "")},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"layered_insert_df_triggers",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? LayeredInsertDFTriggerScript(qname, token_ctes, input_id)
                : "")},
       {"stats_delta_update",
        SQLTemplateArgument::TrustedSQL(StatsDeltaUpdateStatement(
            qname, input_id, input_values.size(), true))}});
}

static string ClusteredInsertTriggerScript(const QualifiedName &qname,
                                           const string &input_id,
                                           const vector<string> &input_values,
                                           const string &stemmer) {
  vector<string> input_value_selects;
  auto tokenize_fields =
      BuildInsertedFieldQueries(qname, input_values, input_value_selects);
  auto input_value_select_list =
      StringUtil::Join(input_value_selects, ",\n                   ");
  auto union_fields_query = StringUtil::Join(tokenize_fields, " UNION ALL ");
  auto docs_new_docs_cte = RenderSQLTemplate(
      fts_sql::INSERT_DOCS_CTE,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"input_value_select_list",
        SQLTemplateArgument::TrustedSQL(input_value_select_list)}});
  auto token_new_docs_cte = RenderSQLTemplate(
      fts_sql::INSERT_TOKEN_DOCS_CTE,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"input_value_select_list",
        SQLTemplateArgument::TrustedSQL(input_value_select_list)}});
  auto token_ctes = RenderSQLTemplate(
      fts_sql::CLUSTERED_INSERT_TOKEN_CTES,
      {{"new_docs_cte", SQLTemplateArgument::TrustedSQL(token_new_docs_cte)},
       {"union_fields_query",
        SQLTemplateArgument::TrustedSQL(union_fields_query)},
       {"stemmer", SQLTemplateArgument::StringLiteral(stemmer)},
       {"fts_schema", GetFTSSchemaArgument(qname)}});
  auto trigger_names = GetFTSClusteredInsertTriggerNames(qname);
  return RenderSQLTemplate(
      fts_sql::CLUSTERED_INSERT_TRIGGERS,
      {{"trigger_00_docs", SQLTemplateArgument::Identifier(trigger_names[0])},
       {"trigger_10_dict_insert",
        SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_20_terms_store",
        SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_30_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_40_stats", SQLTemplateArgument::Identifier(trigger_names[4])},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"fts_schema", GetFTSSchemaArgument(qname)},
       {"docs_new_docs_cte",
        SQLTemplateArgument::TrustedSQL(docs_new_docs_cte)},
       {"union_fields_query",
        SQLTemplateArgument::TrustedSQL(union_fields_query)},
       {"stemmer", SQLTemplateArgument::StringLiteral(stemmer)},
       {"field_length_aggregates",
        SQLTemplateArgument::TrustedSQL(
            FieldLengthAggregateList(input_values.size()))},
       {"zero_field_lengths",
        SQLTemplateArgument::TrustedSQL(
            ZeroFieldLengthList(input_values.size(), "BIGINT"))},
       {"token_ctes", SQLTemplateArgument::TrustedSQL(token_ctes)},
       {"terms_storage_table",
        SQLTemplateArgument::TrustedSQL(GetFTSTermsStorageTable())},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"stats_delta_update",
        SQLTemplateArgument::TrustedSQL(StatsDeltaUpdateStatement(
            qname, input_id, input_values.size(), true))}});
}

static string DeleteTriggerScript(const QualifiedName &qname,
                                  const string &input_id,
                                  const vector<string> &input_values,
                                  const string &stemmer, bool layered_search) {
  vector<string> tokenize_fields;
  for (auto &input_value : input_values) {
    tokenize_fields.push_back(RenderSQLTemplate(
        fts_sql::TOKENIZE_DELETED_FIELD,
        {{"fts_schema", GetFTSSchemaArgument(qname)},
         {"input_value", SQLTemplateArgument::Identifier(input_value)}}));
  }
  auto token_ctes = RenderSQLTemplate(
      fts_sql::DELETE_TOKEN_CTES,
      {{"union_fields_query", SQLTemplateArgument::TrustedSQL(StringUtil::Join(
                                  tokenize_fields, " UNION ALL "))},
       {"stemmer", SQLTemplateArgument::StringLiteral(stemmer)},
       {"fts_schema", GetFTSSchemaArgument(qname)}});
  auto trigger_names = GetFTSDeleteTriggerNames(qname);
  return RenderSQLTemplate(
      fts_sql::DELETE_TRIGGERS,
      {{"trigger_00_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[0])},
       {"trigger_10_terms", SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_15_stats", SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_20_docs", SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_30_dict_prune",
        SQLTemplateArgument::Identifier(trigger_names[4])},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"fts_schema", GetFTSSchemaArgument(qname)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"layered_delete_before_terms_triggers",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? LayeredDeleteBeforeTermsTriggerScript(qname, input_id)
                : "")},
       {"stats_delta_update",
        SQLTemplateArgument::TrustedSQL(StatsDeltaUpdateStatement(
            qname, input_id, input_values.size(), false))},
       {"layered_delete_after_df_triggers",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? LayeredDeleteAfterDFTriggerScript(qname, token_ctes)
                : "")}});
}

static string ClusteredDeleteTriggerScript(const QualifiedName &qname,
                                           const string &input_id,
                                           idx_t field_count) {
  auto trigger_names = GetFTSClusteredDeleteTriggerNames(qname);
  return RenderSQLTemplate(
      fts_sql::CLUSTERED_DELETE_TRIGGERS,
      {{"trigger_00_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[0])},
       {"trigger_10_terms_store",
        SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_15_stats", SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_20_docs", SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_30_dict_prune",
        SQLTemplateArgument::Identifier(trigger_names[4])},
       {"terms_storage_table",
        SQLTemplateArgument::TrustedSQL(GetFTSTermsStorageTable())},
       {"input_table", GetQualifiedTableArgument(qname)},
       {"fts_schema", GetFTSSchemaArgument(qname)},
       {"stats_delta_update",
        SQLTemplateArgument::TrustedSQL(
            StatsDeltaUpdateStatement(qname, input_id, field_count, false))},
       {"input_id", SQLTemplateArgument::Identifier(input_id)}});
}

string FTSIndexMaintenance::Create(const QualifiedName &qname,
                                   const string &input_id,
                                   const vector<string> &input_values,
                                   const string &stemmer, bool cluster_terms,
                                   bool layered_search) {
  string result = IncrementalIndexSetupScript(qname);
  if (layered_search) {
    result += InsertTriggerScript(qname, input_id, input_values, stemmer, true);
    result += DeleteTriggerScript(qname, input_id, input_values, stemmer, true);
  } else if (cluster_terms) {
    result += ClusteredIncrementalIndexSetupScript(qname);
    result +=
        ClusteredInsertTriggerScript(qname, input_id, input_values, stemmer);
    result +=
        ClusteredDeleteTriggerScript(qname, input_id, input_values.size());
  } else {
    result +=
        InsertTriggerScript(qname, input_id, input_values, stemmer, false);
    result +=
        DeleteTriggerScript(qname, input_id, input_values, stemmer, false);
  }
  return result;
}

} // namespace duckdb
