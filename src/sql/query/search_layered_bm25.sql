CREATE MACRO {{fts_schema}}.search_layered_bm25(query_string, fields := NULL, top_k := 50, k := 1.2, b := 0.75, term_limit := 32, max_df_ratio := 0.15, max_df := 50000, enable_prefix := true, enable_substring := true, enable_fuzzy := true, enable_short_fuzzy := true, expand_exact_terms := false, query_mode := 'standard', field_weights := NULL, field_b := NULL, scoring_model := 'bm25f', tie_breaker := 0.0) AS TABLE
WITH params(term_limit, max_df_ratio, max_df, enable_prefix, enable_substring, enable_fuzzy, enable_short_fuzzy, expand_exact_terms, query_mode, field_weights, field_b, scoring_model, tie_breaker, default_b) AS (
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
           field_b::MAP(VARCHAR, DOUBLE),
           lower(scoring_model::VARCHAR),
           tie_breaker::DOUBLE,
           b::DOUBLE
),
{{field_scoring_config_ctes}}
df_cap(max_df) AS (
    SELECT least(params.max_df, ceil(stats.num_docs * params.max_df_ratio)::BIGINT)
    FROM {{fts_schema}}.stats AS stats
    CROSS JOIN params
),
tokenized_query AS (
    SELECT unnest(tokens) AS raw_token,
           generate_subscripts(tokens, 1) AS token_position
    FROM (
        SELECT {{fts_schema}}.tokenize(query_string) AS tokens
    ) AS query_token_list
),
raw_tokens AS (
    SELECT raw_token,
           token_position,
           max(token_position) OVER () AS final_position
    FROM tokenized_query
    WHERE raw_token IS NOT NULL
      AND raw_token <> ''
      AND raw_token NOT IN (SELECT sw FROM {{fts_schema}}.stopwords)
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
        SELECT stem(raw_token, {{stemmer}}) AS query_term
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
    JOIN {{fts_schema}}.term_stats AS term_stats
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
    JOIN {{fts_schema}}.term_grams AS term_grams
      ON term_grams.gram = query_grams.gram
    JOIN {{fts_schema}}.term_stats AS term_stats
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
    JOIN {{fts_schema}}.term_stats AS term_stats
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
    JOIN {{fts_schema}}.term_stats_by_len AS term_stats
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
    JOIN {{fts_schema}}.term_prefixes AS term_prefixes
      ON term_prefixes.prefix_len = final_token.prefix_len
     AND term_prefixes.prefix = final_token.prefix
    JOIN {{fts_schema}}.raw_dict AS raw_dict
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
field_term_tf AS (
    SELECT selected_terms.termid,
           selected_terms.rawtermid,
           terms.docid,
           terms.fieldid,
           selected_terms.df,
           max(selected_terms.expansion_weight) AS expansion_weight,
           count(*) AS tf
    FROM selected_terms
    JOIN {{fts_schema}}.terms AS terms
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
{{field_scoring_score_ctes}},
ranked AS (
    SELECT docs.name AS docname,
           scores.score,
           row_number() OVER (ORDER BY scores.score DESC, docs.name) AS rank
    FROM scores
    JOIN {{fts_schema}}.docs AS docs ON docs.docid = scores.docid
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
