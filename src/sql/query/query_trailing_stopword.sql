UNION ALL
SELECT tokenized.raw_term AS raw_token,
       NULL::VARCHAR AS term,
       tokenized.token_position
FROM tokenized
JOIN (SELECT DISTINCT sw FROM {{fts_schema}}.stopwords) AS stopwords
  ON stopwords.sw = tokenized.raw_term
WHERE lower(query_mode::VARCHAR) = 'phrase_prefix'
  AND tokenized.token_position = tokenized.last_position
