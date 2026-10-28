#include "fts_index_maintenance.hpp"

#include "fts_index_common.hpp"
#include "fts_sql_assets.hpp"
#include "fts_sql_template.hpp"

#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/common/string_util.hpp"

#include <algorithm>

namespace duckdb {

// One group of maintenance triggers. An insert phase indexes the documents in
// `rows` and a delete phase removes them from the index. INSERT and DELETE
// each use a single phase over their transition table. UPDATE runs a delete
// phase over the old row images followed by an insert phase over the new ones.
struct TriggerPhase {
  //! Trigger name infix, see GetFTSInsertTriggerNames
  string name_infix;
  string event;
  string transition_tables;
  bool insert;
  //! Row source of the documents this phase indexes or removes
  string rows;
};

static vector<TriggerPhase>
GetTriggerPhases(const string &input_id, const vector<string> &input_values) {
  vector<string> columns{SQLIdentifier::ToString(input_id)};
  for (auto &input_value : input_values) {
    auto column = SQLIdentifier::ToString(input_value);
    if (std::find(columns.begin(), columns.end(), column) == columns.end()) {
      columns.push_back(column);
    }
  }
  auto column_list = StringUtil::Join(columns, ", ");
  // An UPDATE only has to reindex rows whose document id or indexed text
  // changed. EXCEPT ALL keeps duplicate ids produced by the UPDATE, so the
  // unique index on docs(name) still rejects them as it does for INSERT.
  auto changed_rows = [&](const string &from, const string &other) {
    return StringUtil::Format(
        "(SELECT %s FROM %s EXCEPT ALL SELECT %s FROM %s)", column_list, from,
        column_list, other);
  };
  const string update_transition_tables =
      "OLD TABLE AS fts_old_rows NEW TABLE AS fts_new_rows";
  return {{"ai", "INSERT", "NEW TABLE AS fts_new_rows", true, "fts_new_rows"},
          {"ad", "DELETE", "OLD TABLE AS fts_old_rows", false, "fts_old_rows"},
          {FTS_UPDATE_DELETE_TRIGGER_INFIX, "UPDATE", update_transition_tables,
           false, changed_rows("fts_old_rows", "fts_new_rows")},
          {FTS_UPDATE_INSERT_TRIGGER_INFIX, "UPDATE", update_transition_tables,
           true, changed_rows("fts_new_rows", "fts_old_rows")}};
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

static string StatsDeltaUpdateStatement(const QualifiedName &qname,
                                        const string &input_id,
                                        idx_t field_count,
                                        const TriggerPhase &phase) {
  auto operation = phase.insert ? "+" : "-";
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
       {"changed_rows", SQLTemplateArgument::TrustedSQL(phase.rows)},
       {"input_id", SQLTemplateArgument::Identifier(input_id)}});
}

static vector<SQLTemplateBinding>
TriggerEventBindings(const QualifiedName &qname, const TriggerPhase &phase) {
  return {{"trigger_event", SQLTemplateArgument::TrustedSQL(phase.event)},
          {"transition_tables",
           SQLTemplateArgument::TrustedSQL(phase.transition_tables)},
          {"input_table", GetQualifiedTableArgument(qname)},
          {"fts_schema", GetFTSSchemaArgument(qname)}};
}

static string RenderTriggers(const SQLTemplateAsset &asset,
                             const QualifiedName &qname,
                             const TriggerPhase &phase,
                             vector<SQLTemplateBinding> bindings) {
  auto event_bindings = TriggerEventBindings(qname, phase);
  bindings.insert(bindings.end(), event_bindings.begin(), event_bindings.end());
  return RenderSQLTemplate(asset, bindings);
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
                                                 const TriggerPhase &phase,
                                                 const string &token_ctes) {
  auto trigger_names = GetFTSLayeredInsertTriggerNames(qname, phase.name_infix);
  return RenderTriggers(
      fts_sql::LAYERED_INSERT_NEW_TERMS, qname, phase,
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
       {"trigger_19_raw_term_grams",
        SQLTemplateArgument::Identifier(trigger_names[5])},
       {"token_ctes", SQLTemplateArgument::TrustedSQL(token_ctes)}});
}

static string LayeredInsertDFTriggerScript(const QualifiedName &qname,
                                           const TriggerPhase &phase,
                                           const string &token_ctes,
                                           const string &input_id) {
  auto trigger_names = GetFTSLayeredInsertTriggerNames(qname, phase.name_infix);
  auto affected_termids = LayeredAffectedTermIdsSubquery(qname, token_ctes);
  return RenderTriggers(
      fts_sql::LAYERED_INSERT_DF, qname, phase,
      {{"trigger_35_term_stats_df",
        SQLTemplateArgument::Identifier(trigger_names[6])},
       {"trigger_36_term_stats_by_len_df",
        SQLTemplateArgument::Identifier(trigger_names[7])},
       {"trigger_37_raw_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[8])},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"new_rows", SQLTemplateArgument::TrustedSQL(phase.rows)},
       {"affected_termids",
        SQLTemplateArgument::TrustedSQL(affected_termids)}});
}

static string LayeredDeleteBeforeTermsTriggerScript(const QualifiedName &qname,
                                                    const TriggerPhase &phase,
                                                    const string &input_id) {
  auto trigger_names = GetFTSLayeredDeleteTriggerNames(qname, phase.name_infix);
  return RenderTriggers(
      fts_sql::LAYERED_DELETE_BEFORE_TERMS, qname, phase,
      {{"trigger_05_raw_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[0])},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"old_rows", SQLTemplateArgument::TrustedSQL(phase.rows)}});
}

static string LayeredDeleteAfterDFTriggerScript(const QualifiedName &qname,
                                                const TriggerPhase &phase,
                                                const string &token_ctes) {
  auto trigger_names = GetFTSLayeredDeleteTriggerNames(qname, phase.name_infix);
  auto affected_termids = LayeredAffectedTermIdsSubquery(qname, token_ctes);
  return RenderTriggers(fts_sql::LAYERED_DELETE_AFTER_DF, qname, phase,
                        {{"trigger_21_term_prefixes_prune",
                          SQLTemplateArgument::Identifier(trigger_names[1])},
                         {"trigger_21_raw_term_grams_prune",
                          SQLTemplateArgument::Identifier(trigger_names[2])},
                         {"trigger_22_raw_dict_prune",
                          SQLTemplateArgument::Identifier(trigger_names[3])},
                         {"trigger_23_term_stats_df",
                          SQLTemplateArgument::Identifier(trigger_names[4])},
                         {"trigger_24_term_stats_by_len_df",
                          SQLTemplateArgument::Identifier(trigger_names[5])},
                         {"trigger_25_term_grams_prune",
                          SQLTemplateArgument::Identifier(trigger_names[6])},
                         {"trigger_26_term_stats_by_len_prune",
                          SQLTemplateArgument::Identifier(trigger_names[7])},
                         {"trigger_27_term_stats_prune",
                          SQLTemplateArgument::Identifier(trigger_names[8])},
                         {"affected_termids",
                          SQLTemplateArgument::TrustedSQL(affected_termids)}});
}

static vector<string> BuildInsertedFieldQueries(
    const QualifiedName &qname, const vector<string> &input_values,
    vector<string> &input_value_selects, bool include_positions) {
  vector<string> tokenize_fields;
  for (auto &input_value : input_values) {
    input_value_selects.push_back(StringUtil::Format(
        "fts_new_rows.%s AS %s", SQLIdentifier::ToString(input_value),
        SQLIdentifier::ToString(input_value)));
    tokenize_fields.push_back(RenderSQLTemplate(
        include_positions ? fts_sql::TOKENIZE_INSERTED_POSITIONAL_FIELD
                          : fts_sql::TOKENIZE_INSERTED_FIELD,
        {{"fts_schema", GetFTSSchemaArgument(qname)},
         {"input_value", SQLTemplateArgument::Identifier(input_value)},
         {"input_value_string",
          SQLTemplateArgument::StringLiteral(input_value)}}));
  }
  return tokenize_fields;
}

static string RenderNewDocsCTE(const SQLTemplateAsset &asset,
                               const QualifiedName &qname,
                               const TriggerPhase &phase,
                               const string &input_id,
                               const string &input_value_select_list) {
  return RenderSQLTemplate(
      asset, {{"fts_schema", GetFTSSchemaArgument(qname)},
              {"input_id", SQLTemplateArgument::Identifier(input_id)},
              {"new_rows", SQLTemplateArgument::TrustedSQL(phase.rows)},
              {"input_value_select_list",
               SQLTemplateArgument::TrustedSQL(input_value_select_list)}});
}

static string InsertTriggerScript(const QualifiedName &qname,
                                  const TriggerPhase &phase,
                                  const string &input_id,
                                  const vector<string> &input_values,
                                  const FTSAnalyzerConfig &analyzer_config,
                                  bool layered_search) {
  vector<string> input_value_selects;
  auto tokenize_fields = BuildInsertedFieldQueries(
      qname, input_values, input_value_selects, layered_search);
  auto input_value_select_list =
      StringUtil::Join(input_value_selects, ",\n                   ");
  auto union_fields_query = StringUtil::Join(tokenize_fields, " UNION ALL ");
  auto docs_new_docs_cte =
      RenderNewDocsCTE(fts_sql::INSERT_DOCS_CTE, qname, phase, input_id,
                       input_value_select_list);
  auto token_new_docs_cte =
      RenderNewDocsCTE(fts_sql::INSERT_TOKEN_DOCS_CTE, qname, phase, input_id,
                       input_value_select_list);
  auto token_ctes = RenderSQLTemplate(
      fts_sql::LAYERED_INSERT_TOKEN_CTES,
      {{"new_docs_cte", SQLTemplateArgument::TrustedSQL(token_new_docs_cte)},
       {"union_fields_query",
        SQLTemplateArgument::TrustedSQL(union_fields_query)},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, analyzer_config,
            layered_search
                ? AnalyzeTokenStreamProjection::DOCID_FIELDID_POSITION
                : AnalyzeTokenStreamProjection::DOCID_FIELDID))}});
  auto trigger_names = GetFTSInsertTriggerNames(qname, phase.name_infix);
  auto raw_dict_join =
      layered_search ? "JOIN " + GetFTSSchema(qname) +
                           ".raw_dict AS rd ON rd.raw_term = ss.raw_term AND "
                           "rd.termid = d.termid"
                     : "";
  return RenderTriggers(
      fts_sql::INSERT_TRIGGERS, qname, phase,
      {{"trigger_00_docs", SQLTemplateArgument::Identifier(trigger_names[0])},
       {"trigger_10_dict_insert",
        SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_20_terms", SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_30_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_40_stats", SQLTemplateArgument::Identifier(trigger_names[4])},
       {"docs_new_docs_cte",
        SQLTemplateArgument::TrustedSQL(docs_new_docs_cte)},
       {"union_fields_query",
        SQLTemplateArgument::TrustedSQL(union_fields_query)},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, analyzer_config,
            AnalyzeTokenStreamProjection::DOCID_FIELDID))},
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
                ? LayeredInsertNewTermsTriggerScript(qname, phase, token_ctes)
                : "")},
       {"terms_insert_columns",
        SQLTemplateArgument::TrustedSQL(
            layered_search ? "docid, fieldid, termid, rawtermid, position"
                           : "docid, fieldid, termid")},
       {"layered_term_select",
        SQLTemplateArgument::TrustedSQL(
            layered_search ? ", rd.rawtermid,\n           ss.position" : "")},
       {"raw_dict_join", SQLTemplateArgument::TrustedSQL(raw_dict_join)},
       {"insert_terms_order_by",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? "ORDER BY d.termid, rd.rawtermid, ss.fieldid, ss.docid"
                : "")},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"new_rows", SQLTemplateArgument::TrustedSQL(phase.rows)},
       {"layered_insert_df_triggers",
        SQLTemplateArgument::TrustedSQL(
            layered_search ? LayeredInsertDFTriggerScript(qname, phase,
                                                          token_ctes, input_id)
                           : "")},
       {"stats_delta_update",
        SQLTemplateArgument::TrustedSQL(StatsDeltaUpdateStatement(
            qname, input_id, input_values.size(), phase))}});
}

static string
ClusteredInsertTriggerScript(const QualifiedName &qname,
                             const TriggerPhase &phase, const string &input_id,
                             const vector<string> &input_values,
                             const FTSAnalyzerConfig &analyzer_config) {
  vector<string> input_value_selects;
  auto tokenize_fields = BuildInsertedFieldQueries(qname, input_values,
                                                   input_value_selects, false);
  auto input_value_select_list =
      StringUtil::Join(input_value_selects, ",\n                   ");
  auto union_fields_query = StringUtil::Join(tokenize_fields, " UNION ALL ");
  auto docs_new_docs_cte =
      RenderNewDocsCTE(fts_sql::INSERT_DOCS_CTE, qname, phase, input_id,
                       input_value_select_list);
  auto token_new_docs_cte =
      RenderNewDocsCTE(fts_sql::INSERT_TOKEN_DOCS_CTE, qname, phase, input_id,
                       input_value_select_list);
  auto token_ctes = RenderSQLTemplate(
      fts_sql::CLUSTERED_INSERT_TOKEN_CTES,
      {{"new_docs_cte", SQLTemplateArgument::TrustedSQL(token_new_docs_cte)},
       {"union_fields_query",
        SQLTemplateArgument::TrustedSQL(union_fields_query)},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, analyzer_config,
            AnalyzeTokenStreamProjection::DOCID_FIELDID))}});
  auto trigger_names =
      GetFTSClusteredInsertTriggerNames(qname, phase.name_infix);
  return RenderTriggers(
      fts_sql::CLUSTERED_INSERT_TRIGGERS, qname, phase,
      {{"trigger_00_docs", SQLTemplateArgument::Identifier(trigger_names[0])},
       {"trigger_10_dict_insert",
        SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_20_terms_store",
        SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_30_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_40_stats", SQLTemplateArgument::Identifier(trigger_names[4])},
       {"docs_new_docs_cte",
        SQLTemplateArgument::TrustedSQL(docs_new_docs_cte)},
       {"union_fields_query",
        SQLTemplateArgument::TrustedSQL(union_fields_query)},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, analyzer_config,
            AnalyzeTokenStreamProjection::DOCID_FIELDID))},
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
       {"new_rows", SQLTemplateArgument::TrustedSQL(phase.rows)},
       {"stats_delta_update",
        SQLTemplateArgument::TrustedSQL(StatsDeltaUpdateStatement(
            qname, input_id, input_values.size(), phase))}});
}

static string DeleteTriggerScript(const QualifiedName &qname,
                                  const TriggerPhase &phase,
                                  const string &input_id,
                                  const vector<string> &input_values,
                                  const FTSAnalyzerConfig &analyzer_config,
                                  bool layered_search) {
  vector<string> tokenize_fields;
  for (auto &input_value : input_values) {
    tokenize_fields.push_back(RenderSQLTemplate(
        fts_sql::TOKENIZE_DELETED_FIELD,
        {{"fts_schema", GetFTSSchemaArgument(qname)},
         {"input_value", SQLTemplateArgument::Identifier(input_value)},
         {"old_rows", SQLTemplateArgument::TrustedSQL(phase.rows)}}));
  }
  auto token_ctes = RenderSQLTemplate(
      fts_sql::DELETE_TOKEN_CTES,
      {{"union_fields_query", SQLTemplateArgument::TrustedSQL(StringUtil::Join(
                                  tokenize_fields, " UNION ALL "))},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, analyzer_config, AnalyzeTokenStreamProjection::NONE))}});
  auto trigger_names = GetFTSDeleteTriggerNames(qname, phase.name_infix);
  return RenderTriggers(
      fts_sql::DELETE_TRIGGERS, qname, phase,
      {{"trigger_00_dict_df",
        SQLTemplateArgument::Identifier(trigger_names[0])},
       {"trigger_10_terms", SQLTemplateArgument::Identifier(trigger_names[1])},
       {"trigger_15_stats", SQLTemplateArgument::Identifier(trigger_names[2])},
       {"trigger_20_docs", SQLTemplateArgument::Identifier(trigger_names[3])},
       {"trigger_30_dict_prune",
        SQLTemplateArgument::Identifier(trigger_names[4])},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"old_rows", SQLTemplateArgument::TrustedSQL(phase.rows)},
       {"layered_delete_before_terms_triggers",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? LayeredDeleteBeforeTermsTriggerScript(qname, phase, input_id)
                : "")},
       {"stats_delta_update",
        SQLTemplateArgument::TrustedSQL(StatsDeltaUpdateStatement(
            qname, input_id, input_values.size(), phase))},
       {"layered_delete_after_df_triggers",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? LayeredDeleteAfterDFTriggerScript(qname, phase, token_ctes)
                : "")}});
}

static string ClusteredDeleteTriggerScript(const QualifiedName &qname,
                                           const TriggerPhase &phase,
                                           const string &input_id,
                                           idx_t field_count) {
  auto trigger_names =
      GetFTSClusteredDeleteTriggerNames(qname, phase.name_infix);
  return RenderTriggers(
      fts_sql::CLUSTERED_DELETE_TRIGGERS, qname, phase,
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
       {"stats_delta_update",
        SQLTemplateArgument::TrustedSQL(
            StatsDeltaUpdateStatement(qname, input_id, field_count, phase))},
       {"input_id", SQLTemplateArgument::Identifier(input_id)},
       {"old_rows", SQLTemplateArgument::TrustedSQL(phase.rows)}});
}

string FTSIndexMaintenance::Create(const QualifiedName &qname,
                                   const string &input_id,
                                   const vector<string> &input_values,
                                   const FTSIndexMaintenanceConfig &config) {
  string result = IncrementalIndexSetupScript(qname);
  auto clustered = config.cluster_terms && !config.layered_search;
  if (clustered) {
    result += ClusteredIncrementalIndexSetupScript(qname);
  }
  for (auto &phase : GetTriggerPhases(input_id, input_values)) {
    if (clustered) {
      result += phase.insert
                    ? ClusteredInsertTriggerScript(
                          qname, phase, input_id, input_values, config.analyzer)
                    : ClusteredDeleteTriggerScript(qname, phase, input_id,
                                                   input_values.size());
    } else {
      result +=
          phase.insert
              ? InsertTriggerScript(qname, phase, input_id, input_values,
                                    config.analyzer, config.layered_search)
              : DeleteTriggerScript(qname, phase, input_id, input_values,
                                    config.analyzer, config.layered_search);
    }
  }
  return result;
}

} // namespace duckdb
