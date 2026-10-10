#include "fts_index_builder.hpp"

#include "fts_index_common.hpp"
#include "fts_index_maintenance.hpp"
#include "fts_sql_assets.hpp"
#include "fts_sql_template.hpp"

#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

static constexpr int64_t FTS_INDEX_FORMAT_VERSION = 4;
static constexpr int64_t FTS_ANALYZER_VERSION = 1;

static string SchemaSetupScript(const QualifiedName &qname) {
  return RenderSQLTemplate(fts_sql::SCHEMA_SETUP,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string StopwordsConfig(const FTSIndexConfig &config) {
  if (config.stopwords_mode == FTSStopwordsMode::NONE) {
    return "none";
  }
  if (config.stopwords_mode == FTSStopwordsMode::ENGLISH) {
    return "english";
  }
  return "table:" + GetQualifiedTableName(config.stopwords_table);
}

static void AppendAnalyzerConfigValue(string &result, const string &name,
                                      const string &value) {
  result += name + ":" + std::to_string(value.size()) + ":" + value + ";";
}

static string AnalyzerConfigBase(const FTSIndexConfig &config) {
  string result;
  AppendAnalyzerConfigValue(result, "version",
                            std::to_string(FTS_ANALYZER_VERSION));
  AppendAnalyzerConfigValue(result, "stemmer", config.stemmer);
  AppendAnalyzerConfigValue(result, "tokenizer", config.tokenizer);
  AppendAnalyzerConfigValue(result, "ignore", config.ignore);
  AppendAnalyzerConfigValue(result, "strip_accents",
                            config.strip_accents ? "true" : "false");
  AppendAnalyzerConfigValue(result, "lower", config.lower ? "true" : "false");
  return result;
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

static string TokenizeSpansMacroScript(const QualifiedName &qname,
                                       const string &tokenizer,
                                       const string &ignore, bool strip_accents,
                                       bool lower) {
  return RenderSQLTemplate(
      fts_sql::TOKENIZE_SPANS_MACRO,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"tokenizer", SQLTemplateArgument::StringLiteral(tokenizer)},
       {"ignore", SQLTemplateArgument::StringLiteral(ignore)},
       {"strip_accents", SQLTemplateArgument::Boolean(strip_accents)},
       {"lower", SQLTemplateArgument::Boolean(lower)}});
}

static string HighlightMacrosScript(const QualifiedName &qname) {
  return RenderSQLTemplate(fts_sql::HIGHLIGHT_MACROS,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
}

static string AnalyzeTextMacroScript(const QualifiedName &qname,
                                     const FTSAnalyzerConfig &config) {
  return RenderSQLTemplate(
      fts_sql::ANALYZE_TEXT_MACRO,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, config, AnalyzeTokenStreamProjection::POSITION_OFFSETS))}});
}

static string IndexTablesScript(const QualifiedName &qname,
                                const string &input_id,
                                const vector<string> &input_values,
                                const string &build_terms_table,
                                const string &build_dict_table,
                                const string &build_raw_dict_table,
                                const FTSAnalyzerConfig &analyzer_config,
                                bool cluster_terms, bool layered_search) {
  vector<string> field_values;
  vector<string> tokenize_fields;
  for (idx_t i = 0; i < input_values.size(); i++) {
    field_values.push_back(StringUtil::Format(
        "(%i, %s)", i, SQLString::ToString(input_values[i])));
    tokenize_fields.push_back(RenderSQLTemplate(
        layered_search ? fts_sql::TOKENIZE_POSITIONAL_FIELD
                       : fts_sql::TOKENIZE_FIELD,
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
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, analyzer_config,
            layered_search
                ? AnalyzeTokenStreamProjection::DOCID_FIELDID_POSITION
                : AnalyzeTokenStreamProjection::DOCID_FIELDID))},
       {"raw_term_output",
        SQLTemplateArgument::TrustedSQL(
            layered_search ? "stemmed_stopped.raw_term," : "")},
       {"build_position_output",
        SQLTemplateArgument::TrustedSQL(
            layered_search ? ",\n       stemmed_stopped.position" : "")},
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
       {"position_select",
        SQLTemplateArgument::TrustedSQL(
            layered_search
                ? ",\n       build_terms.position::UINTEGER AS position"
                : "")},
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

static string IndexMetadataScript(const FTSIndexConfig &config) {
  auto analyzer_config = AnalyzerConfigBase(config);
  return RenderSQLTemplate(
      fts_sql::INDEX_METADATA,
      {{"fts_schema", GetFTSSchemaArgument(config.input_table)},
       {"format_version",
        SQLTemplateArgument::Integer(FTS_INDEX_FORMAT_VERSION)},
       {"analyzer_version", SQLTemplateArgument::Integer(FTS_ANALYZER_VERSION)},
       {"incremental", SQLTemplateArgument::Boolean(config.incremental)},
       {"cluster_terms", SQLTemplateArgument::Boolean(config.cluster_terms)},
       {"layered_search", SQLTemplateArgument::Boolean(config.layered_search)},
       {"positions", SQLTemplateArgument::Boolean(config.layered_search)},
       {"stemmer", SQLTemplateArgument::StringLiteral(config.stemmer)},
       {"stopwords",
        SQLTemplateArgument::StringLiteral(StopwordsConfig(config))},
       {"tokenizer", SQLTemplateArgument::StringLiteral(config.tokenizer)},
       {"ignore", SQLTemplateArgument::StringLiteral(config.ignore)},
       {"strip_accents", SQLTemplateArgument::Boolean(config.strip_accents)},
       {"lower", SQLTemplateArgument::Boolean(config.lower)},
       {"analyzer_config",
        SQLTemplateArgument::StringLiteral(analyzer_config)}});
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
        SQLTemplateArgument::TrustedSQL(GetFTSTermPrefixesPrefixIndex(qname))},
       {"raw_term_grams_gram_index",
        SQLTemplateArgument::TrustedSQL(GetFTSRawTermGramsGramIndex(qname))}});
}

static string
FieldScoringValidationCTEs(const QualifiedName &qname,
                           const string &params_name = "params",
                           const string &field_weights_name = "field_weights",
                           const string &field_b_name = "field_b",
                           const string &scoring_model_name = "scoring_model",
                           const string &tie_breaker_name = "tie_breaker",
                           const string &default_b_name = "default_b",
                           bool validate_requested_fields = true) {
  // Checked before the weighting maps because 'fields' selects what is searched
  // at all. An empty 'fields' selects nothing and returns no rows, which
  // test_indexing pins deliberately.
  auto requested_fields_validation =
      validate_requested_fields
          ? "\n    UNION ALL\n"
            "    SELECT 15 AS priority,\n"
            "           'fields contains unknown field: ' || field AS message\n"
            "    FROM requested_fields\n"
            "    WHERE field NOT IN (SELECT field FROM " +
                GetFTSSchema(qname) + ".fields)"
          : "";
  return RenderSQLTemplate(
      fts_sql::FIELD_SCORING_VALIDATION_CTES,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"params", SQLTemplateArgument::Identifier(params_name)},
       {"field_weights", SQLTemplateArgument::Identifier(field_weights_name)},
       {"field_b", SQLTemplateArgument::Identifier(field_b_name)},
       {"scoring_model", SQLTemplateArgument::Identifier(scoring_model_name)},
       {"tie_breaker", SQLTemplateArgument::Identifier(tie_breaker_name)},
       {"default_b", SQLTemplateArgument::Identifier(default_b_name)},
       {"requested_fields_validation",
        SQLTemplateArgument::TrustedSQL(requested_fields_validation)}});
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
                               const FTSAnalyzerConfig &config) {
  return RenderSQLTemplate(
      fts_sql::MATCH_BM25,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, config, AnalyzeTokenStreamProjection::NONE))},
       {"field_scoring_config_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringConfigCTEs(qname))},
       {"field_scoring_score_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringScoreCTEs(qname))}});
}

static string LayeredSearchTableMacroScript(const QualifiedName &qname,
                                            const FTSAnalyzerConfig &config) {
  auto is_final_token =
      config.filter_stopwords
          ? ",\n           generate_subscripts(tokens, 1) = len(tokens) "
            "AS is_final_token"
          : "";
  auto phrase_prefix_stopword_token =
      config.filter_stopwords
          ? "UNION ALL\n"
            "    SELECT tokenized.raw_term AS raw_token,\n"
            "           NULL::VARCHAR AS term,\n"
            "           tokenized.token_position\n"
            "    FROM tokenized\n"
            "    CROSS JOIN params\n"
            "    WHERE params.query_mode = 'phrase_prefix'\n"
            "      AND tokenized.is_final_token\n"
            "      AND tokenized.raw_term IN (\n"
            "          SELECT sw\n"
            "          FROM " +
                GetFTSSchema(qname) +
                ".stopwords\n"
                "      )"
          : "";
  return RenderSQLTemplate(
      fts_sql::SEARCH_LAYERED_BM25,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"is_final_token", SQLTemplateArgument::TrustedSQL(is_final_token)},
       {"analyzed_tokens",
        SQLTemplateArgument::TrustedSQL(RenderAnalyzeTokenStream(
            qname, config, AnalyzeTokenStreamProjection::TOKEN_POSITION))},
       {"phrase_prefix_stopword_token",
        SQLTemplateArgument::TrustedSQL(phrase_prefix_stopword_token)},
       {"field_scoring_config_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringConfigCTEs(qname))},
       {"field_scoring_score_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringScoreCTEs(qname))}});
}

static string LayeredPatternTableMacroScript(const QualifiedName &qname) {
  return RenderSQLTemplate(
      fts_sql::SEARCH_LAYERED_PATTERN,
      {{"fts_schema", GetFTSSchemaArgument(qname)},
       {"field_scoring_config_ctes",
        SQLTemplateArgument::TrustedSQL(FieldScoringConfigCTEs(qname))}});
}

static string LayeredSearchDispatchMacroScript(const QualifiedName &qname) {
  return RenderSQLTemplate(fts_sql::SEARCH_LAYERED_BM25_DISPATCH,
                           {{"fts_schema", GetFTSSchemaArgument(qname)}});
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
            "scoring_model", "scoring_tie_breaker", "bm25_b",
            /* validate_requested_fields */ false))}});
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
                                       const FTSAnalyzerConfig &analyzer_config,
                                       bool include_structured_queries) {
  auto result = LayeredSearchTableMacroScript(qname, analyzer_config) +
                LayeredPatternTableMacroScript(qname) +
                LayeredSearchDispatchMacroScript(qname) +
                LayeredMatchMacroScript(qname);
  if (include_structured_queries) {
    result += StructuredLayeredSearchMacroScript(qname);
  }
  return result;
}

string FTSIndexBuilder::Create(const FTSIndexConfig &config,
                               bool drop_existing_triggers) {
  auto &qname = config.input_table;
  FTSAnalyzerConfig analyzer_config;
  analyzer_config.stemmer = config.stemmer;
  analyzer_config.filter_stopwords =
      config.stopwords_mode != FTSStopwordsMode::NONE;
  string result;
  if (drop_existing_triggers) {
    result += FTSIndexMaintenance::DropTriggers(qname);
  }
  result += SchemaSetupScript(qname);
  result += StopwordsScript(config);
  result += TokenizeMacroScript(qname, config.tokenizer, config.ignore,
                                config.strip_accents, config.lower);
  result += TokenizeSpansMacroScript(qname, config.tokenizer, config.ignore,
                                     config.strip_accents, config.lower);
  result += AnalyzeTextMacroScript(qname, analyzer_config);
  result += HighlightMacrosScript(qname);
  result += IndexTablesScript(
      qname, config.input_id, config.input_values, GetFTSBuildTermsTable(qname),
      GetFTSBuildDictTable(qname), GetFTSBuildRawDictTable(qname),
      analyzer_config, config.cluster_terms, config.layered_search);
  result += IndexMetadataScript(config);
  result += MatchMacroScript(qname, analyzer_config);
  if (config.layered_search) {
    result += LayeredSidecarScript(qname);
    result += LayeredSearchMacroScript(qname, analyzer_config,
                                       config.structured_queries);
  }
  if (config.incremental) {
    FTSIndexMaintenanceConfig maintenance_config;
    maintenance_config.analyzer = analyzer_config;
    maintenance_config.cluster_terms = config.cluster_terms;
    maintenance_config.layered_search = config.layered_search;
    result += FTSIndexMaintenance::Create(
        qname, config.input_id, config.input_values, maintenance_config);
  }
  return result;
}

string
FTSIndexBuilder::CreateStructuredQueryMacros(const QualifiedName &qname) {
  return StructuredLayeredSearchMacroScript(qname);
}

} // namespace duckdb
