CREATE TABLE {{fts_schema}}.fields (fieldid BIGINT, field VARCHAR);
INSERT INTO {{fts_schema}}.fields VALUES {{field_values}};

DROP TABLE IF EXISTS temp.{{build_terms_table}};
DROP TABLE IF EXISTS temp.{{build_dict_table}};
DROP TABLE IF EXISTS temp.{{build_raw_dict_table}};

CREATE TEMP TABLE {{build_terms_table}} AS
WITH tokenized AS (
    {{union_fields_query}}
),
stemmed_stopped AS (
    SELECT {{term_expression}} AS term,
           {{raw_term_select}}
           t.docid AS docid,
           t.fieldid AS fieldid
    FROM tokenized AS t
    WHERE t.w NOT NULL
      AND t.w <> ''
      {{stopwords_filter}}
)
SELECT ss.term,
       {{raw_term_output}}
       ss.docid,
       ss.fieldid
FROM stemmed_stopped AS ss;

CREATE TABLE {{fts_schema}}.docs AS
WITH lengths AS (
    SELECT docid,
           count(*)::BIGINT AS len,
           {{field_length_aggregates}} AS field_lens
    FROM temp.{{build_terms_table}}
    GROUP BY docid
)
SELECT fts_docs.rowid AS docid,
       fts_docs.{{input_id}} AS name,
       coalesce(lengths.len, 0)::BIGINT AS len,
       coalesce(lengths.field_lens, {{zero_field_lengths}})::BIGINT[] AS field_lens
FROM {{input_table}} AS fts_docs
LEFT JOIN lengths ON lengths.docid = fts_docs.rowid
ORDER BY fts_docs.rowid;

CREATE TEMP TABLE {{build_dict_table}} AS
WITH grouped_terms AS (
    SELECT term,
           min(docid) AS first_docid
    FROM temp.{{build_terms_table}}
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

{{build_raw_dict}}

CREATE TABLE {{fts_schema}}.terms AS
SELECT build_dict.termid,
       {{rawtermid_select}}
       build_terms.docid,
       build_terms.fieldid
FROM temp.{{build_terms_table}} AS build_terms
JOIN temp.{{build_dict_table}} AS build_dict
  ON build_dict.term = build_terms.term
{{raw_dict_join}}
{{terms_order_by}};

CREATE TABLE {{fts_schema}}.dict AS
WITH document_frequencies AS (
    SELECT termid,
           count(DISTINCT docid)::BIGINT AS df
    FROM {{fts_schema}}.terms
    GROUP BY termid
)
SELECT build_dict.termid,
       build_dict.term,
       document_frequencies.df
FROM temp.{{build_dict_table}} AS build_dict
JOIN document_frequencies
  ON document_frequencies.termid = build_dict.termid
ORDER BY build_dict.termid;

{{raw_dict_table}}

DROP TABLE temp.{{build_terms_table}};
DROP TABLE temp.{{build_dict_table}};
DROP TABLE IF EXISTS temp.{{build_raw_dict_table}};

CREATE TABLE {{fts_schema}}.stats AS (
    SELECT count(docs.docid)::BIGINT AS num_docs,
           avg(docs.len)::DOUBLE AS avgdl,
           CASE WHEN count(docs.docid) = 0
               THEN NULL::DOUBLE[]
               ELSE {{average_field_lengths}}
           END AS avg_field_lens,
           coalesce(sum(docs.len), 0)::HUGEINT AS total_len,
           {{total_field_lengths}} AS total_field_lens
    FROM {{fts_schema}}.docs AS docs
);
