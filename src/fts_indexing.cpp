#include "fts_indexing.hpp"

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_search_path.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/client_data.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/storage/storage_manager.hpp"

namespace duckdb {

static QualifiedName GetQualifiedName(ClientContext &context,
                                      const string &qname_str) {
  auto qname = QualifiedName::Parse(qname_str);
  if (qname.schema == INVALID_SCHEMA) {
    qname.schema =
        ClientData::Get(context).catalog_search_path->GetDefaultSchema(
            qname.catalog);
  }
  return qname;
}

static string GetFTSSchemaName(const QualifiedName &qname) {
  return StringUtil::Format("fts_%s_%s", qname.schema.GetIdentifierName(),
                            qname.name.GetIdentifierName());
}

static string GetFTSSchema(const QualifiedName &qname) {
  auto result =
      IsInvalidCatalog(qname.catalog)
          ? string("")
          : SQLIdentifier::ToString(qname.catalog.GetIdentifierName()) + ".";
  result += SQLIdentifier::ToString(GetFTSSchemaName(qname));
  return result;
}

static string GetQualifiedTableName(const QualifiedName &qname) {
  vector<string> parts;
  if (!IsInvalidCatalog(qname.catalog)) {
    parts.push_back(SQLIdentifier::ToString(qname.catalog.GetIdentifierName()));
  }
  parts.push_back(SQLIdentifier::ToString(qname.schema.GetIdentifierName()));
  parts.push_back(SQLIdentifier::ToString(qname.name.GetIdentifierName()));
  return StringUtil::Join(parts, ".");
}

static vector<string> GetFTSTriggerNames(const QualifiedName &qname) {
  auto prefix = StringUtil::Format("__fts_%s_ai_", GetFTSSchemaName(qname));
  return {prefix + "00_docs", prefix + "10_dict_insert", prefix + "20_terms",
          prefix + "30_dict_df", prefix + "40_stats"};
}

static bool TableExists(ClientContext &context, const QualifiedName &qname) {
  return Catalog::GetEntry<TableCatalogEntry>(
             context, qname.catalog, qname.schema, qname.name,
             OnEntryNotFound::RETURN_NULL) != nullptr;
}

static bool SupportsFTSTriggers(ClientContext &context,
                                const QualifiedName &qname) {
  auto &catalog = Catalog::GetCatalog(context, qname.catalog);
  auto &attached = catalog.GetAttached();
  if (!attached.HasStorageManager()) {
    return true;
  }
  auto &storage_manager = attached.GetStorageManager();
  return storage_manager.InMemory() ||
         storage_manager.GetStorageVersion() >= StorageVersion::V2_0_0;
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

static string TokenizeMacroScript(const string &ignore, bool strip_accents,
                                  bool lower) {
  string expr = "s::VARCHAR";
  if (strip_accents) {
    expr = "strip_accents(" + expr + ")";
  }
  if (lower) {
    expr = "lower(" + expr + ")";
  }
  expr = "regexp_replace(" + expr + ", $$" + ignore + "$$, ' ', 'g')";
  expr = "string_split_regex(" + expr + ", '\\s+')";
  return "CREATE MACRO %fts_schema%.tokenize(s) AS " + expr + ";";
}

static string IndexTablesScript(const string &input_id,
                                const vector<string> &input_values) {
  // clang-format off
	string result = R"(
        CREATE TABLE %fts_schema%.docs AS (
            SELECT rowid AS docid,
                   %input_id% AS name
            FROM %input_table%
        );

        CREATE TABLE %fts_schema%.fields (fieldid BIGINT, field VARCHAR);
        INSERT INTO %fts_schema%.fields VALUES %field_values%;

        CREATE TABLE %fts_schema%.terms AS
        WITH tokenized AS (
            %union_fields_query%
        ),
        stemmed_stopped AS (
            SELECT stem(t.w, '%stemmer%') AS term,
                   t.docid AS docid,
                   t.fieldid AS fieldid
            FROM tokenized AS t
            WHERE t.w NOT NULL
              AND len(t.w) > 0
              AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)
        )
        SELECT ss.term,
               ss.docid,
               ss.fieldid
        FROM stemmed_stopped AS ss;

        ALTER TABLE %fts_schema%.docs ADD len BIGINT;
        UPDATE %fts_schema%.docs d
        SET len = (
            SELECT count(term)
            FROM %fts_schema%.terms AS t
            WHERE t.docid = d.docid
        );

        CREATE TABLE %fts_schema%.dict AS
        WITH distinct_terms AS (
            SELECT DISTINCT term
            FROM %fts_schema%.terms
            ORDER BY docid, term
        )
        SELECT row_number() OVER () - 1 AS termid,
               dt.term
        FROM distinct_terms AS dt;

        ALTER TABLE %fts_schema%.terms ADD termid BIGINT;
        UPDATE %fts_schema%.terms t
        SET termid = (
            SELECT termid
            FROM %fts_schema%.dict d
            WHERE t.term = d.term
        );
        ALTER TABLE %fts_schema%.terms DROP term;

        ALTER TABLE %fts_schema%.dict ADD df BIGINT;
        UPDATE %fts_schema%.dict d
        SET df = (
            SELECT count(distinct docid)
            FROM %fts_schema%.terms t
            WHERE d.termid = t.termid
            GROUP BY termid
        );

        CREATE TABLE %fts_schema%.stats AS (
            SELECT COUNT(docs.docid) AS num_docs,
                   SUM(docs.len) / COUNT(docs.len) AS avgdl
            FROM %fts_schema%.docs AS docs
        );
    )";

	// Each field gets its own tokenize sub-query; they are unioned to retain the source field.
	string tokenize_field_query = R"(
        SELECT unnest(%fts_schema%.tokenize(fts_ii.%input_value%)) AS w,
               rowid AS docid,
               (SELECT fieldid FROM %fts_schema%.fields WHERE field = %input_value_string%) AS fieldid
        FROM %input_table% AS fts_ii
    )";
  // clang-format on

  vector<string> field_values;
  vector<string> tokenize_fields;
  for (idx_t i = 0; i < input_values.size(); i++) {
    field_values.push_back(StringUtil::Format(
        "(%i, %s)", i, SQLString::ToString(input_values[i])));
    auto query = StringUtil::Replace(tokenize_field_query, "%input_value%",
                                     SQLIdentifier::ToString(input_values[i]));
    query = StringUtil::Replace(query, "%input_value_string%",
                                SQLString::ToString(input_values[i]));
    tokenize_fields.push_back(query);
  }
  result = StringUtil::Replace(result, "%field_values%",
                               StringUtil::Join(field_values, ", "));
  result =
      StringUtil::Replace(result, "%union_fields_query%",
                          StringUtil::Join(tokenize_fields, " UNION ALL "));
  result = StringUtil::Replace(result, "%input_id%",
                               SQLIdentifier::ToString(input_id));
  return result;
}

static string MatchMacroScript() {
  // clang-format off
	return R"(
        CREATE MACRO %fts_schema%.match_bm25(docname, query_string, fields := NULL, k := 1.2, b := 0.75, conjunctive := false) AS (
            WITH tokens AS (
                SELECT DISTINCT stem(unnest(%fts_schema%.tokenize(query_string)), '%stemmer%') AS t
            ),
            fieldids AS (
                SELECT fieldid
                FROM %fts_schema%.fields
                WHERE CASE WHEN fields IS NULL THEN 1 ELSE field IN (SELECT * FROM (SELECT UNNEST(string_split(fields, ','))) AS fsq) END
            ),
            qtermids AS (
                SELECT termid
                FROM %fts_schema%.dict AS dict,
                     tokens
                WHERE dict.term = tokens.t
            ),
            qterms AS (
                SELECT termid,
                       docid
                FROM %fts_schema%.terms AS terms
                WHERE CASE WHEN fields IS NULL THEN 1 ELSE fieldid IN (SELECT * FROM fieldids) END
                  AND termid IN (SELECT qtermids.termid FROM qtermids)
            ),
            term_tf AS (
                SELECT termid,
                       docid,
                       COUNT(*) AS tf
                FROM qterms
                GROUP BY docid,
                         termid
            ),
            cdocs AS (
                SELECT docid
                FROM qterms
                GROUP BY docid
                HAVING CASE WHEN conjunctive THEN COUNT(DISTINCT termid) = (SELECT COUNT(*) FROM tokens) ELSE 1 END
            ),
            subscores AS (
                SELECT docs.docid,
                       len,
                       term_tf.termid,
                       tf,
                       df,
                       (log(((SELECT num_docs FROM %fts_schema%.stats) - df + 0.5) / (df + 0.5) + 1) * ((tf * (k + 1)/(tf + k * (1 - b + b * (len / (SELECT avgdl FROM %fts_schema%.stats))))))) AS subscore
                FROM term_tf,
                     cdocs,
                     %fts_schema%.docs AS docs,
                     %fts_schema%.dict AS dict
                WHERE term_tf.docid = cdocs.docid
                  AND term_tf.docid = docs.docid
                  AND term_tf.termid = dict.termid
            ),
            scores AS (
                SELECT docid,
                       sum(subscore) AS score
                FROM subscores
                GROUP BY docid
            )
            SELECT score
            FROM scores,
                 %fts_schema%.docs AS docs
            WHERE scores.docid = docs.docid
              AND docs.name = docname
        );
    )";
  // clang-format on
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

static string InsertTriggerScript(const QualifiedName &qname,
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
              AND len(t.w) > 0
              AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)
        )
    )";

	string result = R"(
        CREATE TRIGGER %trigger_00_docs% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.docs (docid, name, len)
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
                  AND len(t.w) > 0
                  AND t.w NOT IN (SELECT sw FROM %fts_schema%.stopwords)
            ),
            lengths AS (
                SELECT docid, count(term) AS len
                FROM stemmed_stopped
                GROUP BY docid
            )
            SELECT nd.docid,
                   nd.name,
                   COALESCE(l.len, 0) AS len
            FROM fts_new_docs AS nd
            LEFT JOIN lengths AS l ON nd.docid = l.docid;

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

        CREATE TRIGGER %trigger_20_terms% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            INSERT INTO %fts_schema%.terms (docid, fieldid, termid)
            %token_ctes%
            SELECT ss.docid,
                   ss.fieldid,
                   d.termid
            FROM stemmed_stopped AS ss
            JOIN %fts_schema%.dict AS d ON ss.term = d.term;

        CREATE TRIGGER %trigger_30_dict_df% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.dict AS d
            SET df = (
                SELECT count(DISTINCT docid)
                FROM %fts_schema%.terms AS t
                WHERE d.termid = t.termid
            );

        CREATE TRIGGER %trigger_40_stats% AFTER INSERT ON %input_table%
        REFERENCING NEW TABLE AS fts_new_rows
        FOR EACH STATEMENT
            UPDATE %fts_schema%.stats
            SET num_docs = (SELECT COUNT(docid) FROM %fts_schema%.docs),
                avgdl = (SELECT SUM(len) / COUNT(len) FROM %fts_schema%.docs);
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

  auto trigger_names = GetFTSTriggerNames(qname);
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

// ---------------------------------------------------------------------------
// Coordinator: assembles all parts and substitutes cross-cutting placeholders
// ---------------------------------------------------------------------------

static string IndexingScript(ClientContext &context, QualifiedName &qname,
                             const string &input_id,
                             const vector<string> &input_values,
                             const string &stemmer, const string &stopwords,
                             const string &ignore, bool strip_accents,
                             bool lower, bool incremental) {
  string result;
  if (TableExists(context, qname) && SupportsFTSTriggers(context, qname)) {
    result += DropFTSTriggersScript(qname);
  }
  result += SchemaSetupScript();
  result += StopwordsScript(stopwords);
  result += TokenizeMacroScript(ignore, strip_accents, lower);
  result += IndexTablesScript(input_id, input_values);
  result += MatchMacroScript();
  if (incremental) {
    result += InsertTriggerScript(qname, input_id, input_values);
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

  if (!Catalog::GetSchema(context, qname.catalog,
                          Identifier(GetFTSSchemaName(qname)),
                          OnEntryNotFound::RETURN_NULL)) {
    throw CatalogException("a FTS index does not exist on table '%s.%s'. "
                           "Create one with 'PRAGMA create_fts_index()'.",
                           qname.schema.GetIdentifierName(),
                           qname.name.GetIdentifierName());
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
  Catalog::GetEntry<TableCatalogEntry>(context, qname.catalog, qname.schema,
                                       qname.name);

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
  const string stopwords = get_string("stopwords", "english");
  const string ignore =
      get_string("ignore", "[0-9!@#$%^&*()_+={}\\[\\]:;<>,.?~\\\\/\\|''\"`-]+");
  const bool strip_accents = get_bool("strip_accents", true);
  const bool lower = get_bool("lower", true);
  const bool overwrite = get_bool("overwrite", false);
  const bool incremental = get_bool("incremental", false);

  if (stopwords != "english" && stopwords != "none") {
    auto sw_qname = GetQualifiedName(context, stopwords);
    Catalog::GetEntry<TableCatalogEntry>(context, sw_qname.catalog,
                                         sw_qname.schema, sw_qname.name);
  }

  const string fts_schema = GetFTSSchema(qname);
  if (Catalog::GetSchema(context, qname.catalog,
                         Identifier(GetFTSSchemaName(qname)),
                         OnEntryNotFound::RETURN_NULL) &&
      !overwrite) {
    throw CatalogException("a FTS index already exists on table '%s.%s'. "
                           "Supply 'overwrite=1' to overwrite, or "
                           "drop the existing index with 'PRAGMA "
                           "drop_fts_index()' before creating a new one.",
                           qname.schema.GetIdentifierName(),
                           qname.name.GetIdentifierName());
  }

  // Positional parameters: table, id column, value column(s)
  const string doc_id = StringValue::Get(parameters.values[1]);
  auto &table = Catalog::GetEntry<TableCatalogEntry>(context, qname.catalog,
                                                     qname.schema, qname.name);
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
                             qname.schema.GetIdentifierName(),
                             qname.name.GetIdentifierName(), col_name);
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

  return IndexingScript(context, qname, doc_id, doc_values, stemmer, stopwords,
                        ignore, strip_accents, lower, incremental);
}

} // namespace duckdb
