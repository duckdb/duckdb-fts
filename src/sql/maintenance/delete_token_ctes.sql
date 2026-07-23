WITH tokenized AS (
    {{union_fields_query}}
),
stemmed_stopped AS (
    SELECT t.w AS raw_term,
           stem(t.w, {{stemmer}}) AS term
    FROM tokenized AS t
    WHERE t.w NOT NULL
      AND t.w <> ''
      AND t.w NOT IN (SELECT sw FROM {{fts_schema}}.stopwords)
)
