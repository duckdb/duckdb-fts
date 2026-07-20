#include "fts_indexing.hpp"

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_search_path.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/client_data.hpp"
#include "duckdb/main/connection.hpp"
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
  return {prefix + "00_dict_df", prefix + "10_terms", prefix + "20_docs",
          prefix + "30_dict_prune", prefix + "40_stats"};
}

static vector<string>
GetFTSClusteredDeleteTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ad_", GetFTSSchemaName(qname));
  return {prefix + "00_dict_df", prefix + "10_terms_store", prefix + "20_docs",
          prefix + "30_dict_prune", prefix + "40_stats"};
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

static string StatsUpdateAssignments() {
  // clang-format off
	return R"(
                num_docs = (SELECT COUNT(docid) FROM %fts_schema%.docs),
                avgdl = (SELECT SUM(len) / COUNT(len) FROM %fts_schema%.docs),
                avg_field_lens = (
                    SELECT list(avg_field_len ORDER BY field_position)
                    FROM (
                        SELECT field_position,
                               avg(field_len)::DOUBLE AS avg_field_len
                        FROM (
                            SELECT unnest(field_lens) AS field_len,
                                   generate_subscripts(field_lens, 1) AS field_position
                            FROM %fts_schema%.docs
                        ) AS expanded_field_lengths
                        GROUP BY field_position
                    ) AS field_averages
                )
    )";
  // clang-format on
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

static string SchemaSetupScript() {
  // clang-format off
	return R"(
        DROP SCHEMA IF EXISTS %fts_schema% CASCADE;
        CREATE SCHEMA %fts_schema%;
        CREATE TABLE %fts_schema%.stopwords (sw VARCHAR);
    )";
  // clang-format on
}

static string StopwordsScript(const string &stopwords) {
  if (stopwords == "none") {
    return "";
  }
  if (stopwords == "english") {
    // Default list of english stopwords from "The SMART system"
    // clang-format off
		return R"(
            INSERT INTO %fts_schema%.stopwords VALUES ('a'), ('a''s'), ('able'), ('about'), ('above'), ('according'), ('accordingly'), ('across'), ('actually'), ('after'), ('afterwards'), ('again'), ('against'), ('ain''t'), ('all'), ('allow'), ('allows'), ('almost'), ('alone'), ('along'), ('already'), ('also'), ('although'), ('always'), ('am'), ('among'), ('amongst'), ('an'), ('and'), ('another'), ('any'), ('anybody'), ('anyhow'), ('anyone'), ('anything'), ('anyway'), ('anyways'), ('anywhere'), ('apart'), ('appear'), ('appreciate'), ('appropriate'), ('are'), ('aren''t'), ('around'), ('as'), ('aside'), ('ask'), ('asking'), ('associated'), ('at'), ('available'), ('away'), ('awfully'), ('b'), ('be'), ('became'), ('because'), ('become'), ('becomes'), ('becoming'), ('been'), ('before'), ('beforehand'), ('behind'), ('being'), ('believe'), ('below'), ('beside'), ('besides'), ('best'), ('better'), ('between'), ('beyond'), ('both'), ('brief'), ('but'), ('by'), ('c'), ('c''mon'), ('c''s'), ('came'), ('can'), ('can''t'), ('cannot'), ('cant'), ('cause'), ('causes'), ('certain'), ('certainly'), ('changes'), ('clearly'), ('co'), ('com'), ('come'), ('comes'), ('concerning'), ('consequently'), ('consider'), ('considering'), ('contain'), ('containing'), ('contains'), ('corresponding'), ('could'), ('couldn''t'), ('course'), ('currently'), ('d'), ('definitely'), ('described'), ('despite'), ('did'), ('didn''t'), ('different'), ('do'), ('does'), ('doesn''t'), ('doing'), ('don''t'), ('done'), ('down'), ('downwards'), ('during'), ('e'), ('each'), ('edu'), ('eg'), ('eight'), ('either'), ('else'), ('elsewhere'), ('enough'), ('entirely'), ('especially'), ('et'), ('etc'), ('even'), ('ever'), ('every'), ('everybody'), ('everyone'), ('everything'), ('everywhere'), ('ex'), ('exactly'), ('example'), ('except'), ('f'), ('far'), ('few'), ('fifth'), ('first'), ('five'), ('followed'), ('following'), ('follows'), ('for'), ('former'), ('formerly'), ('forth'), ('four'), ('from'), ('further'), ('furthermore'), ('g'), ('get'), ('gets'), ('getting'), ('given'), ('gives'), ('go'), ('goes'), ('going'), ('gone'), ('got'), ('gotten'), ('greetings'), ('h'), ('had'), ('hadn''t'), ('happens'), ('hardly'), ('has'), ('hasn''t'), ('have'), ('haven''t'), ('having'), ('he'), ('he''s'), ('hello'), ('help'), ('hence'), ('her'), ('here'), ('here''s'), ('hereafter'), ('hereby'), ('herein'), ('hereupon'), ('hers'), ('herself'), ('hi'), ('him'), ('himself'), ('his'), ('hither'), ('hopefully'), ('how'), ('howbeit'), ('however'), ('i'), ('i''d'), ('i''ll'), ('i''m'), ('i''ve'), ('ie'), ('if'), ('ignored'), ('immediate'), ('in'), ('inasmuch'), ('inc'), ('indeed'), ('indicate'), ('indicated'), ('indicates'), ('inner'), ('insofar'), ('instead'), ('into'), ('inward'), ('is'), ('isn''t'), ('it'), ('it''d'), ('it''ll'), ('it''s'), ('its'), ('itself'), ('j'), ('just'), ('k'), ('keep'), ('keeps'), ('kept'), ('know'), ('knows'), ('known'), ('l'), ('last'), ('lately'), ('later'), ('latter'), ('latterly'), ('least'), ('less'), ('lest'), ('let'), ('let''s'), ('like'), ('liked'), ('likely'), ('little'), ('look'), ('looking'), ('looks'), ('ltd'), ('m'), ('mainly'), ('many'), ('may'), ('maybe'), ('me'), ('mean'), ('meanwhile'), ('merely'), ('might'), ('more'), ('moreover'), ('most'), ('mostly'), ('much'), ('must'), ('my'), ('myself'), ('n'), ('name'), ('namely'), ('nd'), ('near'), ('nearly'), ('necessary'), ('need'), ('needs'), ('neither'), ('never'), ('nevertheless'), ('new'), ('next'), ('nine'), ('no'), ('nobody'), ('non'), ('none'), ('noone'), ('nor'), ('normally'), ('not'), ('nothing'), ('novel'), ('now'), ('nowhere'), ('o'), ('obviously'), ('of'), ('off'), ('often'), ('oh'), ('ok'), ('okay'), ('old'), ('on'), ('once'), ('one'), ('ones'), ('only'), ('onto'), ('or'), ('other'), ('others'), ('otherwise'), ('ought'), ('our'), ('ours'), ('ourselves'), ('out'), ('outside'), ('over'), ('overall'), ('own');
            INSERT INTO %fts_schema%.stopwords VALUES ('p'), ('particular'), ('particularly'), ('per'), ('perhaps'), ('placed'), ('please'), ('plus'), ('possible'), ('presumably'), ('probably'), ('provides'), ('q'), ('que'), ('quite'), ('qv'), ('r'), ('rather'), ('rd'), ('re'), ('really'), ('reasonably'), ('regarding'), ('regardless'), ('regards'), ('relatively'), ('respectively'), ('right'), ('s'), ('said'), ('same'), ('saw'), ('say'), ('saying'), ('says'), ('second'), ('secondly'), ('see'), ('seeing'), ('seem'), ('seemed'), ('seeming'), ('seems'), ('seen'), ('self'), ('selves'), ('sensible'), ('sent'), ('serious'), ('seriously'), ('seven'), ('several'), ('shall'), ('she'), ('should'), ('shouldn''t'), ('since'), ('six'), ('so'), ('some'), ('somebody'), ('somehow'), ('someone'), ('something'), ('sometime'), ('sometimes'), ('somewhat'), ('somewhere'), ('soon'), ('sorry'), ('specified'), ('specify'), ('specifying'), ('still'), ('sub'), ('such'), ('sup'), ('sure'), ('t'), ('t''s'), ('take'), ('taken'), ('tell'), ('tends'), ('th'), ('than'), ('thank'), ('thanks'), ('thanx'), ('that'), ('that''s'), ('thats'), ('the'), ('their'), ('theirs'), ('them'), ('themselves'), ('then'), ('thence'), ('there'), ('there''s'), ('thereafter'), ('thereby'), ('therefore'), ('therein'), ('theres'), ('thereupon'), ('these'), ('they'), ('they''d'), ('they''ll'), ('they''re'), ('they''ve'), ('think'), ('third'), ('this'), ('thorough'), ('thoroughly'), ('those'), ('though'), ('three'), ('through'), ('throughout'), ('thru'), ('thus'), ('to'), ('together'), ('too'), ('took'), ('toward'), ('towards'), ('tried'), ('tries'), ('truly'), ('try'), ('trying'), ('twice'), ('two'), ('u'), ('un'), ('under'), ('unfortunately'), ('unless'), ('unlikely'), ('until'), ('unto'), ('up'), ('upon'), ('us'), ('use'), ('used'), ('useful'), ('uses'), ('using'), ('usually'), ('uucp'), ('v'), ('value'), ('various'), ('very'), ('via'), ('viz'), ('vs'), ('w'), ('want'), ('wants'), ('was'), ('wasn''t'), ('way'), ('we'), ('we''d'), ('we''ll'), ('we''re'), ('we''ve'), ('welcome'), ('well'), ('went'), ('were'), ('weren''t'), ('what'), ('what''s'), ('whatever'), ('when'), ('whence'), ('whenever'), ('where'), ('where''s'), ('whereafter'), ('whereas'), ('whereby'), ('wherein'), ('whereupon'), ('wherever'), ('whether'), ('which'), ('while'), ('whither'), ('who'), ('who''s'), ('whoever'), ('whole'), ('whom'), ('whose'), ('why'), ('will'), ('willing'), ('wish'), ('with'), ('within'), ('without'), ('won''t'), ('wonder'), ('would'), ('would'), ('wouldn''t'), ('x'), ('y'), ('yes'), ('yet'), ('you'), ('you''d'), ('you''ll'), ('you''re'), ('you''ve'), ('your'), ('yours'), ('yourself'), ('yourselves'), ('z'), ('zero');
        )";
    // clang-format on
  }
  // Custom stopwords table
  return "INSERT INTO %fts_schema%.stopwords SELECT * FROM " + stopwords + ";";
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

static string TokenizeMacroScript(const string &tokenizer, const string &ignore,
                                  bool strip_accents, bool lower) {
  auto expr = NormalizeTokenInputExpression(strip_accents, lower);
  if (tokenizer == "opensearch_standard") {
    expr = "fts_tokenize_opensearch_standard(" + expr + ")";
    return "CREATE MACRO %fts_schema%.tokenize(s) AS " + expr + ";";
  }
  auto delimiter = "(" + ignore + ")|\\s+";
  expr = "string_split_regex(" + expr + ", $$" + delimiter + "$$)";
  return "CREATE MACRO %fts_schema%.tokenize(s) AS " + expr + ";";
}

static string IndexTablesScript(const string &input_id,
                                const vector<string> &input_values,
                                const string &stemmer, const string &stopwords,
                                const string &build_terms_table,
                                const string &build_dict_table,
                                const string &build_raw_dict_table,
                                bool cluster_terms, bool layered_search) {
  // clang-format off
	string result = R"(
        CREATE TABLE %fts_schema%.fields (fieldid BIGINT, field VARCHAR);
        INSERT INTO %fts_schema%.fields VALUES %field_values%;

        DROP TABLE IF EXISTS temp.%build_terms_table%;
        DROP TABLE IF EXISTS temp.%build_dict_table%;
        DROP TABLE IF EXISTS temp.%build_raw_dict_table%;

        CREATE TEMP TABLE %build_terms_table% AS
        WITH tokenized AS (
            %union_fields_query%
        ),
        stemmed_stopped AS (
            SELECT %term_expression% AS term,
                   %raw_term_select%
                   t.docid AS docid,
                   t.fieldid AS fieldid
            FROM tokenized AS t
            WHERE t.w NOT NULL
              AND t.w <> ''
              %stopwords_filter%
        )
        SELECT ss.term,
               %raw_term_output%
               ss.docid,
               ss.fieldid
        FROM stemmed_stopped AS ss;

        CREATE TABLE %fts_schema%.docs AS
        WITH term_field_lengths AS (
            SELECT docid,
                   fieldid,
                   count(*)::BIGINT AS field_len
            FROM temp.%build_terms_table%
            GROUP BY docid,
                     fieldid
        ),
        field_lengths AS (
            SELECT fts_docs.rowid AS docid,
                   fts_docs.%input_id% AS name,
                   fts_fields.fieldid,
                   coalesce(term_field_lengths.field_len, 0)::BIGINT AS field_len
            FROM %input_table% AS fts_docs
            CROSS JOIN %fts_schema%.fields AS fts_fields
            LEFT JOIN term_field_lengths
              ON term_field_lengths.docid = fts_docs.rowid
             AND term_field_lengths.fieldid = fts_fields.fieldid
        )
        SELECT docid,
               name,
               sum(field_len)::BIGINT AS len,
               list(field_len ORDER BY fieldid) AS field_lens
        FROM field_lengths
        GROUP BY docid,
                 name
        ORDER BY docid;

        CREATE TEMP TABLE %build_dict_table% AS
        WITH grouped_terms AS (
            SELECT term,
                   min(docid) AS first_docid
            FROM temp.%build_terms_table%
            GROUP BY term
        ),
        numbered_terms AS (
            SELECT row_number() OVER (ORDER BY first_docid, term) - 1 AS termid,
                   term
            FROM grouped_terms
        )
        SELECT termid,
               term
        FROM numbered_terms;

        %build_raw_dict%

        CREATE TABLE %fts_schema%.terms AS
        SELECT build_dict.termid,
               %rawtermid_select%
               build_terms.docid,
               build_terms.fieldid
        FROM temp.%build_terms_table% AS build_terms
        JOIN temp.%build_dict_table% AS build_dict
          ON build_dict.term = build_terms.term
        %raw_dict_join%
        %terms_order_by%;

        CREATE TABLE %fts_schema%.dict AS
        WITH document_frequencies AS (
            SELECT termid,
                   count(DISTINCT docid)::BIGINT AS df
            FROM %fts_schema%.terms
            GROUP BY termid
        )
        SELECT build_dict.termid,
               build_dict.term,
               document_frequencies.df
        FROM temp.%build_dict_table% AS build_dict
        JOIN document_frequencies
          ON document_frequencies.termid = build_dict.termid
        ORDER BY build_dict.termid;

        %raw_dict_table%

        DROP TABLE temp.%build_terms_table%;
        DROP TABLE temp.%build_dict_table%;
        DROP TABLE IF EXISTS temp.%build_raw_dict_table%;

        CREATE TABLE %fts_schema%.stats AS (
            WITH expanded_field_lengths AS (
                SELECT unnest(docs.field_lens) AS field_len,
                       generate_subscripts(docs.field_lens, 1) AS field_position
                FROM %fts_schema%.docs AS docs
            ),
            field_averages AS (
                SELECT field_position,
                       avg(field_len)::DOUBLE AS avg_field_len
                FROM expanded_field_lengths
                GROUP BY field_position
            )
            SELECT COUNT(docs.docid) AS num_docs,
                   SUM(docs.len) / COUNT(docs.len) AS avgdl,
                   (SELECT list(avg_field_len ORDER BY field_position)
                    FROM field_averages) AS avg_field_lens
            FROM %fts_schema%.docs AS docs
        );
    )";

	// Each field gets its own tokenize sub-query; they are unioned to retain the source field.
	string tokenize_field_query = R"(
        SELECT unnest(%fts_schema%.tokenize(fts_ii.%input_value%)) AS w,
               fts_ii.rowid AS docid,
               %field_id% AS fieldid
        FROM %input_table% AS fts_ii
    )";
  // clang-format on

  string term_expression = stemmer == "none" ? "t.w" : "stem(t.w, '%stemmer%')";
  string stopwords_filter =
      stopwords == "none"
          ? string("")
          : "AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)";

  vector<string> field_values;
  vector<string> tokenize_fields;
  for (idx_t i = 0; i < input_values.size(); i++) {
    field_values.push_back(StringUtil::Format(
        "(%i, %s)", i, SQLString::ToString(input_values[i])));
    auto query = StringUtil::Replace(tokenize_field_query, "%input_value%",
                                     SQLIdentifier::ToString(input_values[i]));
    query =
        StringUtil::Replace(query, "%field_id%", StringUtil::Format("%i", i));
    tokenize_fields.push_back(query);
  }
  result =
      StringUtil::Replace(result, "%build_terms_table%", build_terms_table);
  result = StringUtil::Replace(result, "%build_dict_table%", build_dict_table);
  result = StringUtil::Replace(result, "%build_raw_dict_table%",
                               build_raw_dict_table);
  string build_raw_dict;
  string raw_dict_table;
  if (layered_search) {
    build_raw_dict = R"(
        CREATE TEMP TABLE %build_raw_dict_table% AS
        WITH grouped_raw_terms AS (
            SELECT raw_term,
                   term,
                   min(docid) AS first_docid
            FROM temp.%build_terms_table%
            GROUP BY raw_term,
                     term
        )
        SELECT row_number() OVER (
                   ORDER BY grouped_raw_terms.first_docid,
                            grouped_raw_terms.raw_term,
                            grouped_raw_terms.term
               ) - 1 AS rawtermid,
               grouped_raw_terms.raw_term,
               build_dict.termid
        FROM grouped_raw_terms
        JOIN temp.%build_dict_table% AS build_dict
          ON build_dict.term = grouped_raw_terms.term;
    )";
    raw_dict_table = R"(
        CREATE TABLE %fts_schema%.raw_dict AS
        WITH document_frequencies AS (
            SELECT rawtermid,
                   count(DISTINCT docid)::BIGINT AS df
            FROM %fts_schema%.terms
            GROUP BY rawtermid
        )
        SELECT build_raw_dict.rawtermid,
               build_raw_dict.raw_term,
               build_raw_dict.termid,
               document_frequencies.df
        FROM temp.%build_raw_dict_table% AS build_raw_dict
        JOIN document_frequencies
          ON document_frequencies.rawtermid = build_raw_dict.rawtermid
        ORDER BY build_raw_dict.rawtermid;
    )";
  }
  result = StringUtil::Replace(result, "%build_raw_dict%", build_raw_dict);
  result = StringUtil::Replace(result, "%raw_dict_table%", raw_dict_table);
  result = StringUtil::Replace(result, "%raw_term_select%",
                               layered_search ? "t.w AS raw_term," : "");
  result = StringUtil::Replace(result, "%raw_term_output%",
                               layered_search ? "ss.raw_term," : "");
  result =
      StringUtil::Replace(result, "%rawtermid_select%",
                          layered_search ? "build_raw_dict.rawtermid," : "");
  result = StringUtil::Replace(
      result, "%raw_dict_join%",
      layered_search ? "JOIN temp.%build_raw_dict_table% AS build_raw_dict ON "
                       "build_raw_dict.raw_term = build_terms.raw_term AND "
                       "build_raw_dict.termid = build_dict.termid"
                     : "");
  result =
      StringUtil::Replace(result, "%build_terms_table%", build_terms_table);
  result = StringUtil::Replace(result, "%build_dict_table%", build_dict_table);
  result = StringUtil::Replace(result, "%build_raw_dict_table%",
                               build_raw_dict_table);
  result = StringUtil::Replace(
      result, "%terms_order_by%",
      cluster_terms
          ? (layered_search
                 ? "ORDER BY build_dict.termid, build_raw_dict.rawtermid, "
                   "build_terms.fieldid, build_terms.docid"
                 : "ORDER BY build_dict.termid, build_terms.fieldid, "
                   "build_terms.docid")
          : "");
  result = StringUtil::Replace(result, "%term_expression%", term_expression);
  result = StringUtil::Replace(result, "%stopwords_filter%", stopwords_filter);
  result = StringUtil::Replace(result, "%field_values%",
                               StringUtil::Join(field_values, ", "));
  result =
      StringUtil::Replace(result, "%union_fields_query%",
                          StringUtil::Join(tokenize_fields, " UNION ALL "));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  return result;
}

static string LayeredSidecarScript(const QualifiedName &qname) {
  // clang-format off
	string result = R"(
        CREATE TABLE %fts_schema%.term_stats AS
        SELECT termid,
               term,
               df,
               length(term)::BIGINT AS term_len,
               greatest(length(term) - 2, 0)::BIGINT AS gram_count
        FROM %fts_schema%.dict
        WHERE term <> '';

        CREATE TABLE %fts_schema%.term_stats_by_len AS
        SELECT termid,
               term,
               df,
               term_len,
               gram_count
        FROM %fts_schema%.term_stats
        ORDER BY term_len,
                 df,
                 termid;

        CREATE TABLE %fts_schema%.term_grams AS
        SELECT 'g' || lower(hex(substr(term, i, 3))) AS gram,
               termid
        FROM %fts_schema%.term_stats,
             range(1, gram_count + 1) AS r(i)
        WHERE gram_count > 0
          AND NOT regexp_full_match(term, '[0-9]+')
        ORDER BY gram,
                 termid;

        CREATE TABLE %fts_schema%.term_prefixes AS
        SELECT prefix_len,
               substr(raw_dict.raw_term, 1, prefix_len) AS prefix,
               raw_dict.rawtermid
        FROM %fts_schema%.raw_dict AS raw_dict,
             (VALUES (2::UTINYINT), (3::UTINYINT)) AS prefix_lengths(prefix_len)
        WHERE length(raw_dict.raw_term) >= prefix_len
        ORDER BY prefix_len,
                 prefix,
                 raw_dict.rawtermid;

        CREATE INDEX %term_stats_term_index% ON %fts_schema%.term_stats(term);
        CREATE INDEX %term_grams_gram_index% ON %fts_schema%.term_grams(gram);
        CREATE INDEX %term_prefixes_prefix_index% ON %fts_schema%.term_prefixes(prefix_len, prefix);

        ANALYZE %fts_schema%.term_stats;
        ANALYZE %fts_schema%.term_stats_by_len;
        ANALYZE %fts_schema%.term_grams;
        ANALYZE %fts_schema%.raw_dict;
        ANALYZE %fts_schema%.term_prefixes;
    )";
  // clang-format on

  result = StringUtil::Replace(result, "%term_stats_term_index%",
                               GetFTSTermStatsTermIndex(qname));
  result = StringUtil::Replace(result, "%term_grams_gram_index%",
                               GetFTSTermGramsGramIndex(qname));
  result = StringUtil::Replace(result, "%term_prefixes_prefix_index%",
                               GetFTSTermPrefixesPrefixIndex(qname));
  return result;
}

static string FieldScoringValidationCTEs() {
  // clang-format off
	return R"(
            weight_entries AS (
                SELECT unnest(map_entries(params.field_weights), recursive := true)
                FROM params
            ),
            validation_errors AS (
                SELECT 'field_mode must be either sum or best_fields' AS message
                FROM params
                WHERE field_mode IS NULL
                   OR field_mode NOT IN ('sum', 'best_fields')
                UNION ALL
                SELECT 'tie_breaker must be finite and between 0 and 1' AS message
                FROM params
                WHERE tie_breaker IS NULL
                   OR NOT isfinite(tie_breaker)
                   OR tie_breaker NOT BETWEEN 0.0 AND 1.0
                UNION ALL
                SELECT 'field weight for ' || key || ' must be finite and non-negative' AS message
                FROM weight_entries
                WHERE value IS NULL
                   OR NOT isfinite(value)
                   OR value < 0.0
                UNION ALL
                SELECT 'field_weights contains unknown field: ' || key AS message
                FROM weight_entries
                WHERE key NOT IN (SELECT field FROM %fts_schema%.fields)
            ),
    )";
  // clang-format on
}

static string MatchMacroScript() {
  // clang-format off
	string result = R"(
        CREATE MACRO %fts_schema%.match_bm25(docname, query_string, fields := NULL, k := 1.2, b := 0.75, conjunctive := false, field_weights := NULL, field_mode := 'sum', tie_breaker := 0.0) AS (
            WITH params AS (
                SELECT field_weights::MAP(VARCHAR, DOUBLE) AS field_weights,
                       lower(field_mode::VARCHAR) AS field_mode,
                       tie_breaker::DOUBLE AS tie_breaker,
                       field_weights IS NULL
                           AND lower(field_mode::VARCHAR) = 'sum'
                           AND tie_breaker::DOUBLE = 0.0 AS compatibility_mode
            ),
            %field_scoring_validation_ctes%
            raw_tokens AS (
                SELECT DISTINCT raw_token
                FROM (
                    SELECT unnest(%fts_schema%.tokenize(query_string)) AS raw_token
                ) AS tokenized_query
                WHERE raw_token IS NOT NULL
                  AND raw_token <> ''
                  AND raw_token NOT IN (SELECT sw FROM %fts_schema%.stopwords)
            ),
            tokens AS (
                SELECT t
                FROM (
                    SELECT DISTINCT stem(raw_token, '%stemmer%') AS t
                    FROM raw_tokens
                ) AS stemmed_tokens
                WHERE t IS NOT NULL
                  AND t <> ''
            ),
            requested_fields AS (
                SELECT trim(field_name) AS field
                FROM (
                    SELECT unnest(string_split(fields, ',')) AS field_name
                ) AS split_fields
                WHERE trim(field_name) <> ''
            ),
            field_config AS (
                SELECT fts_fields.fieldid,
                       coalesce(
                           map_extract_value(params.field_weights, fts_fields.field),
                           1.0
                       )::DOUBLE AS field_weight
                FROM %fts_schema%.fields AS fts_fields
                CROSS JOIN params
                WHERE CASE WHEN fields IS NULL THEN 1 ELSE fts_fields.field IN (SELECT requested_fields.field FROM requested_fields) END
            ),
            qtermids AS (
                SELECT termid
                FROM %fts_schema%.dict AS dict,
                     tokens
                WHERE dict.term = tokens.t
            ),
            qterms AS (
                SELECT termid,
                       docid,
                       fieldid
                FROM %fts_schema%.terms AS terms
                WHERE fieldid IN (SELECT fieldid FROM field_config)
                  AND termid IN (SELECT qtermids.termid FROM qtermids)
            ),
            compatibility_term_tf AS (
                SELECT termid,
                       docid,
                       COUNT(*) AS tf
                FROM qterms
                GROUP BY docid,
                         termid
            ),
            field_term_tf AS (
                SELECT termid,
                       docid,
                       fieldid,
                       count(*) AS tf
                FROM qterms
                GROUP BY docid,
                         fieldid,
                         termid
            ),
            cdocs AS (
                SELECT docid
                FROM qterms
                GROUP BY docid
                HAVING CASE WHEN conjunctive THEN COUNT(DISTINCT termid) = (SELECT COUNT(*) FROM tokens) ELSE 1 END
            ),
            compatibility_subscores AS (
                SELECT docs.docid,
                       len,
                       compatibility_term_tf.termid,
                       tf,
                       df,
                       (log(((SELECT num_docs FROM %fts_schema%.stats) - df + 0.5) / (df + 0.5) + 1) * ((tf * (k + 1)/(tf + k * (1 - b + b * (len / (SELECT avgdl FROM %fts_schema%.stats))))))) AS subscore
                FROM compatibility_term_tf,
                     cdocs,
                     %fts_schema%.docs AS docs,
                     %fts_schema%.dict AS dict
                WHERE compatibility_term_tf.docid = cdocs.docid
                  AND compatibility_term_tf.docid = docs.docid
                  AND compatibility_term_tf.termid = dict.termid
            ),
            compatibility_scores AS (
                SELECT docid,
                       sum(subscore) AS score
                FROM compatibility_subscores
                GROUP BY docid
            ),
            field_subscores AS (
                SELECT field_term_tf.docid,
                       field_term_tf.fieldid,
                       field_config.field_weight,
                       log(((stats.num_docs - dict.df + 0.5) / (dict.df + 0.5)) + 1)
                       * (
                           (field_term_tf.tf * (k + 1))
                           / (
                               field_term_tf.tf
                               + k * (
                                   1 - b
                                   + b * (
                                       list_extract(docs.field_lens, field_term_tf.fieldid + 1)
                                       / list_extract(stats.avg_field_lens, field_term_tf.fieldid + 1)
                                   )
                               )
                           )
                       ) AS subscore
                FROM field_term_tf
                JOIN cdocs ON cdocs.docid = field_term_tf.docid
                JOIN %fts_schema%.docs AS docs ON docs.docid = field_term_tf.docid
                JOIN %fts_schema%.dict AS dict ON dict.termid = field_term_tf.termid
                JOIN field_config ON field_config.fieldid = field_term_tf.fieldid
                CROSS JOIN %fts_schema%.stats AS stats
            ),
            per_field_scores AS (
                SELECT docid,
                       fieldid,
                       max(field_weight) * sum(subscore) AS field_score
                FROM field_subscores
                GROUP BY docid,
                         fieldid
            ),
            field_aware_scores AS (
                SELECT per_field_scores.docid,
                       CASE params.field_mode
                           WHEN 'sum' THEN sum(field_score)
                           WHEN 'best_fields' THEN max(field_score)
                               + params.tie_breaker * (sum(field_score) - max(field_score))
                       END AS score
                FROM per_field_scores
                CROSS JOIN params
                GROUP BY per_field_scores.docid,
                         params.field_mode,
                         params.tie_breaker
            ),
            scores AS (
                SELECT compatibility_scores.*
                FROM compatibility_scores
                CROSS JOIN params
                WHERE params.compatibility_mode
                UNION ALL
                SELECT field_aware_scores.*
                FROM field_aware_scores
                CROSS JOIN params
                WHERE NOT params.compatibility_mode
            )
            SELECT score
            FROM scores,
                 %fts_schema%.docs AS docs
            WHERE scores.docid = docs.docid
              AND docs.name = docname
            UNION ALL
            SELECT error(message)::DOUBLE
            FROM validation_errors
        );
    )";
  // clang-format on
  return StringUtil::Replace(result, "%field_scoring_validation_ctes%",
                             FieldScoringValidationCTEs());
}

static string LayeredSearchTableMacroScript() {
  // clang-format off
	string result = R"(
		CREATE MACRO %fts_schema%.search_layered_bm25(query_string, fields := NULL, top_k := 50, k := 1.2, b := 0.75, term_limit := 32, max_df_ratio := 0.15, max_df := 50000, enable_prefix := true, enable_substring := true, enable_fuzzy := true, enable_short_fuzzy := true, expand_exact_terms := false, query_mode := 'standard', field_weights := NULL, field_mode := 'sum', tie_breaker := 0.0) AS TABLE
			WITH params(term_limit, max_df_ratio, max_df, enable_prefix, enable_substring, enable_fuzzy, enable_short_fuzzy, expand_exact_terms, query_mode, field_weights, field_mode, tie_breaker, compatibility_mode) AS (
                SELECT term_limit::BIGINT,
                       max_df_ratio::DOUBLE,
                       max_df::BIGINT,
                       enable_prefix::BOOLEAN,
                       enable_substring::BOOLEAN,
                       enable_fuzzy::BOOLEAN,
                       enable_short_fuzzy::BOOLEAN,
                       expand_exact_terms::BOOLEAN,
                       CASE lower(query_mode::VARCHAR)
                           WHEN 'standard' THEN 'standard'
                           WHEN 'autocomplete' THEN 'autocomplete'
                           ELSE error('query_mode must be either standard or autocomplete')
                       END,
                       field_weights::MAP(VARCHAR, DOUBLE),
                       lower(field_mode::VARCHAR),
                       tie_breaker::DOUBLE,
                       field_weights IS NULL
                           AND lower(field_mode::VARCHAR) = 'sum'
                           AND tie_breaker::DOUBLE = 0.0
            ),
            %field_scoring_validation_ctes%
            df_cap(max_df) AS (
                SELECT least(params.max_df, ceil(stats.num_docs * params.max_df_ratio)::BIGINT)
                FROM %fts_schema%.stats AS stats
                CROSS JOIN params
            ),
            tokenized_query AS (
                SELECT unnest(tokens) AS raw_token,
                       generate_subscripts(tokens, 1) AS token_position
                FROM (
                    SELECT %fts_schema%.tokenize(query_string) AS tokens
                ) AS query_token_list
            ),
            raw_tokens AS (
                SELECT raw_token,
                       token_position,
                       max(token_position) OVER () AS final_position
                FROM tokenized_query
                WHERE raw_token IS NOT NULL
                  AND raw_token <> ''
                  AND raw_token NOT IN (SELECT sw FROM %fts_schema%.stopwords)
            ),
            autocomplete_final_token AS (
                SELECT raw_token AS query_term,
                       length(raw_token)::BIGINT AS query_len,
                       least(length(raw_token), 3)::UTINYINT AS prefix_len,
                       substr(raw_token, 1, least(length(raw_token), 3)) AS prefix
                FROM raw_tokens
                CROSS JOIN params
                WHERE params.query_mode = 'autocomplete'
                  AND token_position = final_position
                  AND length(raw_token) >= 2
            ),
            stemmed_tokens AS (
                SELECT DISTINCT query_term
                FROM (
                    SELECT stem(raw_token, '%stemmer%') AS query_term
                    FROM raw_tokens
                    CROSS JOIN params
                    WHERE params.query_mode = 'standard'
                       OR (
                           token_position < final_position
                           AND EXISTS (SELECT 1 FROM autocomplete_final_token)
                       )
                ) AS stemmed_query
                WHERE query_term IS NOT NULL
                  AND query_term <> ''
            ),
            query_tokens AS (
                SELECT query_term,
                       length(query_term)::BIGINT AS query_len,
                       greatest(length(query_term) - 2, 0)::BIGINT AS query_gram_count,
                       regexp_full_match(query_term, '[0-9]+') AS is_numeric
                FROM stemmed_tokens
            ),
            exact_terms AS (
                SELECT query_tokens.query_term,
                       term_stats.termid,
                       NULL::BIGINT AS rawtermid,
                       term_stats.term,
                       term_stats.df,
                       1.0::DOUBLE AS expansion_weight,
                       'exact' AS match_type
                FROM query_tokens
                JOIN %fts_schema%.term_stats AS term_stats
                  ON term_stats.term = query_tokens.query_term
            ),
            expansion_tokens AS (
                SELECT query_tokens.*
                FROM query_tokens
                LEFT JOIN exact_terms
                  ON exact_terms.query_term = query_tokens.query_term
                CROSS JOIN params
                WHERE params.query_mode = 'standard'
                  AND (
                      params.expand_exact_terms
                      OR exact_terms.termid IS NULL
                  )
            ),
            query_grams AS (
                SELECT query_term,
                       'g' || lower(hex(substr(query_term, i, 3))) AS gram
                FROM expansion_tokens,
                     range(1, query_gram_count + 1) AS r(i)
                WHERE query_gram_count > 0
                  AND NOT is_numeric
            ),
            gram_candidates AS (
                SELECT query_grams.query_term,
                       term_grams.termid,
                       count(*)::BIGINT AS matching_grams
                FROM query_grams
                JOIN %fts_schema%.term_grams AS term_grams
                  ON term_grams.gram = query_grams.gram
                JOIN %fts_schema%.term_stats AS term_stats
                  ON term_stats.termid = term_grams.termid
                CROSS JOIN df_cap
                WHERE term_stats.df <= df_cap.max_df
                GROUP BY query_grams.query_term,
                         term_grams.termid
            ),
            gram_expansions AS (
                SELECT gram_candidates.query_term,
                       term_stats.termid,
                       NULL::BIGINT AS rawtermid,
                       term_stats.term,
                       term_stats.df,
                       CASE
                           WHEN params.enable_prefix
                            AND starts_with(term_stats.term, expansion_tokens.query_term)
                            AND term_stats.term <> expansion_tokens.query_term
                               THEN 0.85::DOUBLE
                           WHEN params.enable_substring
                            AND contains(term_stats.term, expansion_tokens.query_term)
                            AND term_stats.term <> expansion_tokens.query_term
                            AND gram_candidates.matching_grams = expansion_tokens.query_gram_count
                               THEN 0.75::DOUBLE
                           WHEN params.enable_fuzzy
                            AND expansion_tokens.query_len >= 3
                            AND gram_candidates.matching_grams >= greatest(1, expansion_tokens.query_gram_count - 1)
                            AND abs(term_stats.term_len - expansion_tokens.query_len) <= CASE
                                   WHEN expansion_tokens.query_len <= 4 THEN 1
                                   WHEN expansion_tokens.query_len <= 8 THEN 2
                                   ELSE 3
                                END
                            AND damerau_levenshtein(expansion_tokens.query_term, term_stats.term) <= CASE
                                   WHEN expansion_tokens.query_len <= 4 THEN 1
                                   WHEN expansion_tokens.query_len <= 8 THEN 2
                                   ELSE 3
                                END
                               THEN greatest(
                                   0.25::DOUBLE,
                                   1.0::DOUBLE - (
                                       damerau_levenshtein(expansion_tokens.query_term, term_stats.term)::DOUBLE
                                       / greatest(expansion_tokens.query_len, term_stats.term_len)::DOUBLE
                                   )
                               )
                           ELSE NULL
                       END AS expansion_weight,
                       CASE
                           WHEN params.enable_prefix
                            AND starts_with(term_stats.term, expansion_tokens.query_term)
                            AND term_stats.term <> expansion_tokens.query_term
                               THEN 'prefix'
                           WHEN params.enable_substring
                            AND contains(term_stats.term, expansion_tokens.query_term)
                            AND term_stats.term <> expansion_tokens.query_term
                            AND gram_candidates.matching_grams = expansion_tokens.query_gram_count
                               THEN 'substring'
                           WHEN params.enable_fuzzy
                            AND expansion_tokens.query_len >= 3
                            AND gram_candidates.matching_grams >= greatest(1, expansion_tokens.query_gram_count - 1)
                            AND abs(term_stats.term_len - expansion_tokens.query_len) <= CASE
                                   WHEN expansion_tokens.query_len <= 4 THEN 1
                                   WHEN expansion_tokens.query_len <= 8 THEN 2
                                   ELSE 3
                                END
                            AND damerau_levenshtein(expansion_tokens.query_term, term_stats.term) <= CASE
                                   WHEN expansion_tokens.query_len <= 4 THEN 1
                                   WHEN expansion_tokens.query_len <= 8 THEN 2
                                   ELSE 3
                                END
                               THEN 'fuzzy'
                           ELSE NULL
                       END AS match_type
                FROM gram_candidates
                JOIN expansion_tokens
                  ON expansion_tokens.query_term = gram_candidates.query_term
                JOIN %fts_schema%.term_stats AS term_stats
                  ON term_stats.termid = gram_candidates.termid
                CROSS JOIN params
            ),
            short_fuzzy_expansions AS (
                SELECT expansion_tokens.query_term,
                       term_stats.termid,
                       NULL::BIGINT AS rawtermid,
                       term_stats.term,
                       term_stats.df,
                       greatest(
                           0.25::DOUBLE,
                           1.0::DOUBLE - (
                               damerau_levenshtein(expansion_tokens.query_term, term_stats.term)::DOUBLE
                               / greatest(expansion_tokens.query_len, term_stats.term_len)::DOUBLE
                           )
                       ) AS expansion_weight,
                       'fuzzy' AS match_type
                FROM expansion_tokens
                JOIN %fts_schema%.term_stats_by_len AS term_stats
                  ON term_stats.term_len BETWEEN expansion_tokens.query_len - 1 AND expansion_tokens.query_len + 1
                CROSS JOIN params
                CROSS JOIN df_cap
                WHERE params.enable_fuzzy
                  AND params.enable_short_fuzzy
                  AND expansion_tokens.query_len BETWEEN 3 AND 5
                  AND NOT expansion_tokens.is_numeric
                  AND term_stats.df <= df_cap.max_df
                  AND term_stats.term <> expansion_tokens.query_term
                  AND damerau_levenshtein(expansion_tokens.query_term, term_stats.term) <= 1
            ),
            expansion_candidates AS (
                SELECT *
                FROM gram_expansions
                WHERE expansion_weight IS NOT NULL
                UNION ALL
                SELECT *
                FROM short_fuzzy_expansions
            ),
            deduped_expansions AS (
                SELECT query_term,
                       termid,
                       rawtermid,
                       term,
                       df,
                       expansion_weight,
                       match_type
                FROM (
                    SELECT *,
                           row_number() OVER (
                               PARTITION BY query_term,
                                            termid,
                                            rawtermid
                               ORDER BY expansion_weight DESC,
                                        df ASC,
                                        length(term) ASC,
                                        term ASC,
                                        termid ASC,
                                        match_type ASC
                           ) AS dedupe_rank
                    FROM expansion_candidates
                ) AS ranked_candidates
                WHERE dedupe_rank = 1
            ),
            limited_expansions AS (
                SELECT *,
                       row_number() OVER (
                           PARTITION BY query_term
                           ORDER BY expansion_weight DESC,
                                    df ASC,
                                    length(term) ASC,
                                    term ASC,
                                    termid ASC
                       ) AS expansion_rank
                FROM deduped_expansions
            ),
            autocomplete_candidates AS (
                SELECT final_token.query_term,
                       raw_dict.termid,
                       raw_dict.rawtermid,
                       raw_dict.raw_term AS term,
                       raw_dict.df,
                       CASE
                           WHEN raw_dict.raw_term = final_token.query_term THEN 1.0::DOUBLE
                           ELSE 0.85::DOUBLE
                       END AS expansion_weight,
                       CASE
                           WHEN raw_dict.raw_term = final_token.query_term THEN 'exact'
                           ELSE 'prefix'
                       END AS match_type
                FROM autocomplete_final_token AS final_token
                JOIN %fts_schema%.term_prefixes AS term_prefixes
                  ON term_prefixes.prefix_len = final_token.prefix_len
                 AND term_prefixes.prefix = final_token.prefix
                JOIN %fts_schema%.raw_dict AS raw_dict
                  ON raw_dict.rawtermid = term_prefixes.rawtermid
                CROSS JOIN df_cap
                WHERE starts_with(raw_dict.raw_term, final_token.query_term)
                  AND (
                      raw_dict.raw_term = final_token.query_term
                      OR raw_dict.df <= df_cap.max_df
                  )
            ),
            autocomplete_exact_terms AS (
                SELECT *
                FROM autocomplete_candidates
                WHERE term = query_term
            ),
            autocomplete_prefix_terms AS (
                SELECT query_term,
                       termid,
                       rawtermid,
                       term,
                       df,
                       expansion_weight,
                       match_type,
                       row_number() OVER (
                           PARTITION BY query_term
                           ORDER BY df ASC,
                                    length(term) ASC,
                                    term ASC,
                                    rawtermid ASC
                       ) AS expansion_rank
                FROM autocomplete_candidates
                WHERE term <> query_term
            ),
            selected_terms AS (
                SELECT query_term,
                       termid,
                       rawtermid,
                       any_value(term) AS term,
                       any_value(df) AS df,
                       any_value(match_type) AS match_type,
                       max(expansion_weight) AS expansion_weight
                FROM (
                    SELECT *
                    FROM exact_terms
                    UNION ALL
                    SELECT query_term,
                           termid,
                           rawtermid,
                           term,
                           df,
                           expansion_weight,
                           match_type
                    FROM limited_expansions
                    CROSS JOIN params
                    WHERE expansion_rank <= params.term_limit
                    UNION ALL
                    SELECT *
                    FROM autocomplete_exact_terms
                    UNION ALL
                    SELECT query_term,
                           termid,
                           rawtermid,
                           term,
                           df,
                           expansion_weight,
                           match_type
                    FROM autocomplete_prefix_terms
                    CROSS JOIN params
                    WHERE expansion_rank <= params.term_limit
                ) AS terms
                GROUP BY query_term,
                         termid,
                         rawtermid
            ),
            field_config AS (
                SELECT fts_fields.fieldid,
                       coalesce(
                           map_extract_value(params.field_weights, fts_fields.field),
                           1.0
                       )::DOUBLE AS field_weight
                FROM %fts_schema%.fields AS fts_fields
                CROSS JOIN params
                WHERE CASE WHEN fields IS NULL THEN true ELSE fts_fields.field IN (
                    SELECT trim(field_name) AS field
                    FROM (
                        SELECT unnest(string_split(fields, ',')) AS field_name
                    ) AS split_fields
                    WHERE trim(field_name) <> ''
                ) END
            ),
            compatibility_term_tf AS (
                SELECT selected_terms.termid,
                       selected_terms.rawtermid,
                       terms.docid,
                       selected_terms.df,
                       max(selected_terms.expansion_weight) AS expansion_weight,
                       count(*) AS tf
                FROM selected_terms
                JOIN %fts_schema%.terms AS terms
                  ON terms.termid = selected_terms.termid
                 AND (
                     selected_terms.rawtermid IS NULL
                     OR terms.rawtermid = selected_terms.rawtermid
                 )
                WHERE terms.fieldid IN (SELECT fieldid FROM field_config)
                GROUP BY selected_terms.termid,
                         selected_terms.rawtermid,
                         terms.docid,
                         selected_terms.df
            ),
            field_term_tf AS (
                SELECT selected_terms.termid,
                       selected_terms.rawtermid,
                       terms.docid,
                       terms.fieldid,
                       selected_terms.df,
                       max(selected_terms.expansion_weight) AS expansion_weight,
                       count(*) AS tf
                FROM selected_terms
                JOIN %fts_schema%.terms AS terms
                  ON terms.termid = selected_terms.termid
                 AND (
                     selected_terms.rawtermid IS NULL
                     OR terms.rawtermid = selected_terms.rawtermid
                 )
                WHERE terms.fieldid IN (SELECT fieldid FROM field_config)
                GROUP BY selected_terms.termid,
                         selected_terms.rawtermid,
                         terms.docid,
                         terms.fieldid,
                         selected_terms.df
            ),
            compatibility_scores AS (
                SELECT docs.name AS docname,
                       sum(
                           compatibility_term_tf.expansion_weight
                           * log(((((stats.num_docs - compatibility_term_tf.df) + 0.5) / (compatibility_term_tf.df + 0.5)) + 1))
                           * (
                               (compatibility_term_tf.tf * (k + 1))
                               / (
                                   compatibility_term_tf.tf
                                   + (
                                       k
                                       * ((1 - b) + (b * (docs.len / stats.avgdl)))
                                   )
                               )
                           )
                       ) AS score
                FROM compatibility_term_tf
                JOIN %fts_schema%.docs AS docs
                  ON docs.docid = compatibility_term_tf.docid
                CROSS JOIN %fts_schema%.stats AS stats
                GROUP BY docs.name
            ),
            field_subscores AS (
                SELECT docs.name AS docname,
                       field_term_tf.fieldid,
                       field_config.field_weight,
                       field_term_tf.expansion_weight
                       * log(((((stats.num_docs - field_term_tf.df) + 0.5) / (field_term_tf.df + 0.5)) + 1))
                       * (
                           (field_term_tf.tf * (k + 1))
                           / (
                               field_term_tf.tf
                               + k * (
                                   1 - b
                                   + b * (
                                       list_extract(docs.field_lens, field_term_tf.fieldid + 1)
                                       / list_extract(stats.avg_field_lens, field_term_tf.fieldid + 1)
                                   )
                               )
                           )
                       ) AS subscore
                FROM field_term_tf
                JOIN %fts_schema%.docs AS docs ON docs.docid = field_term_tf.docid
                JOIN field_config ON field_config.fieldid = field_term_tf.fieldid
                CROSS JOIN %fts_schema%.stats AS stats
            ),
            per_field_scores AS (
                SELECT docname,
                       fieldid,
                       max(field_weight) * sum(subscore) AS field_score
                FROM field_subscores
                GROUP BY docname,
                         fieldid
            ),
            field_aware_scores AS (
                SELECT per_field_scores.docname,
                       CASE params.field_mode
                           WHEN 'sum' THEN sum(field_score)
                           WHEN 'best_fields' THEN max(field_score)
                               + params.tie_breaker * (sum(field_score) - max(field_score))
                       END AS score
                FROM per_field_scores
                CROSS JOIN params
                GROUP BY per_field_scores.docname,
                         params.field_mode,
                         params.tie_breaker
            ),
            scores AS (
                SELECT compatibility_scores.*
                FROM compatibility_scores
                CROSS JOIN params
                WHERE params.compatibility_mode
                UNION ALL
                SELECT field_aware_scores.*
                FROM field_aware_scores
                CROSS JOIN params
                WHERE NOT params.compatibility_mode
            ),
            ranked AS (
                SELECT docname,
                       score,
                       row_number() OVER (ORDER BY score DESC, docname) AS rank
                FROM scores
            ),
            results AS (
                SELECT docname,
                       score,
                       rank
                FROM ranked
                WHERE top_k IS NULL
                   OR rank <= top_k
            )
            SELECT *
            FROM results
            UNION ALL
            SELECT error(message)::VARCHAR AS docname,
                   NULL::DOUBLE AS score,
                   NULL::BIGINT AS rank
            FROM validation_errors
            ORDER BY rank;
    )";
  // clang-format on
  return StringUtil::Replace(result, "%field_scoring_validation_ctes%",
                             FieldScoringValidationCTEs());
}

static string LayeredMatchMacroScript() {
  // clang-format off
	return R"(
		CREATE MACRO %fts_schema%.match_layered_bm25(input_id, query_string, fields := NULL, k := 1.2, b := 0.75, term_limit := 32, max_df_ratio := 0.15, max_df := 50000, enable_prefix := true, enable_substring := true, enable_fuzzy := true, enable_short_fuzzy := true, expand_exact_terms := false, query_mode := 'standard', field_weights := NULL, field_mode := 'sum', tie_breaker := 0.0) AS (
            SELECT score
            FROM %fts_schema%.search_layered_bm25(
                query_string,
                fields := fields,
                top_k := NULL::BIGINT,
                k := k,
                b := b,
                term_limit := term_limit,
                max_df_ratio := max_df_ratio,
                max_df := max_df,
                enable_prefix := enable_prefix,
                enable_substring := enable_substring,
                enable_fuzzy := enable_fuzzy,
                enable_short_fuzzy := enable_short_fuzzy,
                expand_exact_terms := expand_exact_terms,
                query_mode := query_mode,
                field_weights := field_weights,
                field_mode := field_mode,
                tie_breaker := tie_breaker
            ) AS hits
            WHERE hits.docname = input_id
        );
    )";
  // clang-format on
}

static string LayeredSearchMacroScript() {
  return LayeredSearchTableMacroScript() + LayeredMatchMacroScript();
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
            token_field_lengths AS (
                SELECT docid,
                       fieldid,
                       count(*)::BIGINT AS field_len
                FROM stemmed_stopped
                GROUP BY docid,
                         fieldid
            ),
            field_lengths AS (
                SELECT nd.docid,
                       nd.name,
                       fts_fields.fieldid,
                       coalesce(token_field_lengths.field_len, 0)::BIGINT AS field_len
                FROM fts_new_docs AS nd
                CROSS JOIN %fts_schema%.fields AS fts_fields
                LEFT JOIN token_field_lengths
                  ON token_field_lengths.docid = nd.docid
                 AND token_field_lengths.fieldid = fts_fields.fieldid
            )
            SELECT docid,
                   name,
                   sum(field_len)::BIGINT AS len,
                   list(field_len ORDER BY fieldid) AS field_lens
            FROM field_lengths
            GROUP BY docid,
                     name;

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
            UPDATE %fts_schema%.stats
            SET %stats_update_assignments%;
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
  result = StringUtil::Replace(result, "%stats_update_assignments%",
                               StatsUpdateAssignments());
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
            token_field_lengths AS (
                SELECT docid,
                       fieldid,
                       count(*)::BIGINT AS field_len
                FROM stemmed_stopped
                GROUP BY docid,
                         fieldid
            ),
            field_lengths AS (
                SELECT nd.docid,
                       nd.name,
                       fts_fields.fieldid,
                       coalesce(token_field_lengths.field_len, 0)::BIGINT AS field_len
                FROM fts_new_docs AS nd
                CROSS JOIN %fts_schema%.fields AS fts_fields
                LEFT JOIN token_field_lengths
                  ON token_field_lengths.docid = nd.docid
                 AND token_field_lengths.fieldid = fts_fields.fieldid
            )
            SELECT docid,
                   name,
                   sum(field_len)::BIGINT AS len,
                   list(field_len ORDER BY fieldid) AS field_lens
            FROM field_lengths
            GROUP BY docid,
                     name;

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
            UPDATE %fts_schema%.stats
            SET %stats_update_assignments%;
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
  result = StringUtil::Replace(result, "%stats_update_assignments%",
                               StatsUpdateAssignments());
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

        CREATE TRIGGER %trigger_40_stats% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.stats
            SET %stats_update_assignments%;
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
  result = StringUtil::Replace(result, "%trigger_20_docs%",
                               SQLIdentifier::ToString(trigger_names[2]));
  result = StringUtil::Replace(result, "%trigger_30_dict_prune%",
                               SQLIdentifier::ToString(trigger_names[3]));
  result = StringUtil::Replace(result, "%trigger_40_stats%",
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
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  result = StringUtil::Replace(result, "%stats_update_assignments%",
                               StatsUpdateAssignments());
  return result;
}

static string ClusteredDeleteTriggerScript(const QualifiedName &qname,
                                           const string &input_id) {
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

        CREATE TRIGGER %trigger_40_stats% AFTER DELETE ON %input_table%
        REFERENCING OLD TABLE AS fts_old_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.stats
            SET %stats_update_assignments%;
    )";
  // clang-format on

  auto trigger_names = GetFTSClusteredDeleteTriggerNames(qname);
  result = StringUtil::Replace(result, "%trigger_00_dict_df%",
                               SQLIdentifier::ToString(trigger_names[0]));
  result = StringUtil::Replace(result, "%trigger_10_terms_store%",
                               SQLIdentifier::ToString(trigger_names[1]));
  result = StringUtil::Replace(result, "%trigger_20_docs%",
                               SQLIdentifier::ToString(trigger_names[2]));
  result = StringUtil::Replace(result, "%trigger_30_dict_prune%",
                               SQLIdentifier::ToString(trigger_names[3]));
  result = StringUtil::Replace(result, "%trigger_40_stats%",
                               SQLIdentifier::ToString(trigger_names[4]));
  result = StringUtil::Replace(result, "%terms_storage_table%",
                               GetFTSTermsStorageTable());
  result = StringUtil::Replace(result, "%input_table%",
                               GetQualifiedTableName(qname));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  result = StringUtil::Replace(result, "%stats_update_assignments%",
                               StatsUpdateAssignments());
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
                             bool cluster_terms, bool layered_search) {
  string result;
  if (TableExists(context, qname) && SupportsFTSTriggers(context, qname)) {
    result += DropFTSTriggersScript(qname);
  }
  result += SchemaSetupScript();
  result += StopwordsScript(stopwords);
  result += TokenizeMacroScript(tokenizer, ignore, strip_accents, lower);
  result += IndexTablesScript(
      input_id, input_values, stemmer, stopwords, GetFTSBuildTermsTable(qname),
      GetFTSBuildDictTable(qname), GetFTSBuildRawDictTable(qname),
      cluster_terms, layered_search);
  result += MatchMacroScript();
  if (layered_search) {
    result += LayeredSidecarScript(qname);
    result += LayeredSearchMacroScript();
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
      result += ClusteredDeleteTriggerScript(qname, input_id);
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

  const string stemmer = get_string("stemmer", "porter");
  const string tokenizer = get_string("tokenizer", "regex");
  const string stopwords = get_string("stopwords", "english");
  const string ignore =
      get_string("ignore", "[0-9!@#$%^&*()_+={}\\[\\]:;<>,.?~\\\\/\\|''\"`-]+");
  const bool strip_accents = get_bool("strip_accents", true);
  const bool lower = get_bool("lower", true);
  const bool overwrite = get_bool("overwrite", false);
  const bool incremental = get_bool("incremental", false);
  const bool layered_search = get_bool("layered_search", false);
  const bool cluster_terms = get_bool("cluster_terms", false) || layered_search;

  if (tokenizer != "regex" && tokenizer != "opensearch_standard") {
    throw InvalidInputException(
        "Unrecognized tokenizer '%s'. Supported tokenizers are: ['regex', "
        "'opensearch_standard']",
        tokenizer);
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
                        cluster_terms, layered_search);
}

} // namespace duckdb
