tokenized AS (
    SELECT raw_term,
           token_position,
           max(token_position) OVER () AS last_position
    FROM (
        SELECT unnest(tokens) AS raw_term,
               generate_subscripts(tokens, 1)::BIGINT AS token_position
        FROM (
            SELECT list_filter({{fts_schema}}.tokenize(query_string),
                               lambda token: token IS NOT NULL AND token <> '') AS tokens
        ) AS query_token_list
    ) AS positioned
),
query_analyzer_tokens AS (
    SELECT analyzed.raw_term AS raw_token,
           analyzed.term,
           analyzed.token_position
    FROM (
        {{analyzed_tokens}}
    ) AS analyzed
    {{trailing_stopword_token}}
),
query_statistics AS (
    SELECT count(*)::BIGINT AS token_count,
           count(DISTINCT term)::BIGINT AS distinct_term_count,
           min(token_position) AS first_position,
           max(token_position) AS final_position,
           arg_max(raw_token, token_position) AS final_raw_token
    FROM query_analyzer_tokens
),
query_shape AS (
    SELECT query_statistics.*,
           CASE
               WHEN lower(query_mode::VARCHAR) = 'phrase' AND token_count = 1 THEN 'standard'
               WHEN lower(query_mode::VARCHAR) = 'phrase_prefix' AND token_count = 1 THEN 'autocomplete'
               WHEN lower(query_mode::VARCHAR) = 'near' AND distinct_term_count = 1 THEN 'standard'
               ELSE lower(query_mode::VARCHAR)
           END AS effective_mode
    FROM query_statistics
),
query_slots AS (
    SELECT query_analyzer_tokens.*,
           token_position - query_shape.first_position AS relative_position,
           CASE
               WHEN query_shape.effective_mode = 'autocomplete'
                    AND length(query_shape.final_raw_token) < 2 THEN 'none'
               WHEN query_shape.effective_mode IN ('autocomplete', 'phrase_prefix')
                    AND token_position = query_shape.final_position
                   THEN CASE WHEN length(raw_token) >= 2 THEN 'prefix' ELSE 'none' END
               ELSE 'term'
           END AS match_kind
    FROM query_analyzer_tokens
    CROSS JOIN query_shape
)
