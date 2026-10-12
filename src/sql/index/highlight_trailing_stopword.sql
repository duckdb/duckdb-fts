UNION ALL
-- A trailing stopword is the unfinished prefix of a phrase_prefix query, as in search_layered_bm25.
SELECT tail.raw_term,
       NULL::VARCHAR AS term,
       tail.position
FROM (
    SELECT raw_term,
           position,
           max(position) OVER () AS last_position
    FROM (
        SELECT token.t.raw_term AS raw_term,
               token.position::UINTEGER AS position
        FROM UNNEST(
            list_filter(
                {{fts_schema}}.tokenize_spans(query_string),
                lambda value: value.raw_term IS NOT NULL AND value.raw_term <> ''
            )
        ) WITH ORDINALITY AS token(t, position)
    ) AS raw_tokens
) AS tail
JOIN {{fts_schema}}.stopwords AS stopwords
  ON stopwords.sw = tail.raw_term
WHERE tail.position = tail.last_position
  AND lower(query_mode::VARCHAR) = 'phrase_prefix'
