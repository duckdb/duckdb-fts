CREATE TRIGGER {{trigger_00_docs}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.docs (docid, name, len, field_lens)
    WITH {{docs_new_docs_cte}},
    tokenized AS (
        {{union_fields_query}}
    ),
    stemmed_stopped AS (
        SELECT stem(t.w, {{stemmer}}) AS term,
               t.docid AS docid,
               t.fieldid AS fieldid
        FROM tokenized AS t
        WHERE t.w NOT NULL
          AND t.w <> ''
          AND t.w NOT IN (SELECT sw FROM {{fts_schema}}.stopwords)
    ),
    lengths AS (
        SELECT docid,
               count(*)::BIGINT AS len,
               {{field_length_aggregates}} AS field_lens
        FROM stemmed_stopped
        GROUP BY docid
    )
    SELECT nd.docid,
           nd.name,
           coalesce(lengths.len, 0)::BIGINT AS len,
           coalesce(lengths.field_lens, {{zero_field_lengths}})::BIGINT[] AS field_lens
    FROM fts_new_docs AS nd
    LEFT JOIN lengths ON lengths.docid = nd.docid;

CREATE TRIGGER {{trigger_10_dict_insert}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.dict (termid, term, df)
    {{token_ctes}},
    new_terms AS (
        SELECT DISTINCT term
        FROM stemmed_stopped
        WHERE term NOT IN (SELECT term FROM {{fts_schema}}.dict)
        ORDER BY term
    )
    SELECT (SELECT COALESCE(max(termid) + 1, 0) FROM {{fts_schema}}.dict) + row_number() OVER () - 1 AS termid,
           term,
           0 AS df
    FROM new_terms;

CREATE TRIGGER {{trigger_20_terms_store}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.{{terms_storage_table}} (termid, docid, fieldid)
    {{token_ctes}}
    SELECT d.termid,
           ss.docid,
           ss.fieldid
    FROM stemmed_stopped AS ss
    JOIN {{fts_schema}}.dict AS d ON ss.term = d.term;

CREATE TRIGGER {{trigger_30_dict_df}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.dict AS d
    SET df = d.df + inserted_df_delta.df_delta
    FROM (
        SELECT t.termid,
               COUNT(DISTINCT t.docid) AS df_delta
        FROM {{fts_schema}}.{{terms_storage_table}} AS t
        JOIN {{fts_schema}}.docs AS docs ON t.docid = docs.docid
        JOIN fts_new_rows AS new_rows ON docs.name = new_rows.{{input_id}}
        GROUP BY t.termid
    ) AS inserted_df_delta
    WHERE d.termid = inserted_df_delta.termid;

CREATE TRIGGER {{trigger_40_stats}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
FOR EACH STATEMENT
    {{stats_delta_update}};
