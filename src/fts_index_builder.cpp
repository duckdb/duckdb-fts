#include "fts_index_builder.hpp"

#include "fts_index_common.hpp"
#include "fts_index_maintenance.hpp"
#include "fts_sql_assets.hpp"
#include "fts_sql_template.hpp"

#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

static string SchemaSetupScript(const QualifiedName &qname) {
  return RenderSQLTemplate(fts_sql::SCHEMA_SETUP,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string StopwordsScript(const FTSIndexConfig &config) {
  if (config.stopwords_mode == FTSStopwordsMode::NONE) {
    return "";
  }
  if (config.stopwords_mode == FTSStopwordsMode::ENGLISH) {
    // Default list of English stopwords from "The SMART system".
    return RenderSQLTemplate(
        fts_sql::ENGLISH_STOPWORDS,
        {{"fts_schema", GetFTSSchemaArgument(config.input_table)}});
  }
  return RenderSQLTemplate(
      fts_sql::IMPORT_STOPWORDS,
      {{"fts_schema", GetFTSSchemaArgument(config.input_table)},
       {"stopwords_table", GetQualifiedTableArgument(config.stopwords_table)}});
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

static string IndexTablesScript(
    const QualifiedName &qname, const string &input_id,
    const vector<string> &input_values, const string &stemmer,
    FTSStopwordsMode stopwords_mode, const string &build_terms_table,
    const string &build_dict_table, const string &build_raw_dict_table,
    bool cluster_terms, bool layered_search) {
  string term_expression =
      stemmer == "none" ? "t.w"
                        : "stem(t.w, " + SQLString::ToString(stemmer) + ")";
  string stopwords_filter = stopwords_mode == FTSStopwordsMode::NONE
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

string FTSIndexBuilder::Create(const FTSIndexConfig &config,
                               bool drop_existing_triggers) {
  auto &qname = config.input_table;
  string result;
  if (drop_existing_triggers) {
    result += FTSIndexMaintenance::DropTriggers(qname);
  }
  result += SchemaSetupScript(qname);
  result += StopwordsScript(config);
  result += TokenizeMacroScript(qname, config.tokenizer, config.ignore,
                                config.strip_accents, config.lower);
  result += IndexTablesScript(
      qname, config.input_id, config.input_values, config.stemmer,
      config.stopwords_mode, GetFTSBuildTermsTable(qname),
      GetFTSBuildDictTable(qname), GetFTSBuildRawDictTable(qname),
      config.cluster_terms, config.layered_search);
  result += MatchMacroScript(qname, config.stemmer);
  if (config.layered_search) {
    result += LayeredSidecarScript(qname);
    result += LayeredSearchMacroScript(qname, config.stemmer,
                                       config.structured_queries);
  }
  if (config.incremental) {
    result += FTSIndexMaintenance::Create(
        qname, config.input_id, config.input_values, config.stemmer,
        config.cluster_terms, config.layered_search);
  }
  return result;
}

string
FTSIndexBuilder::CreateStructuredQueryMacros(const QualifiedName &qname) {
  return StructuredLayeredSearchMacroScript(qname);
}

} // namespace duckdb
