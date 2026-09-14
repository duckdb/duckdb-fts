CREATE MACRO {{fts_schema}}.__search_layered_bm25_non_pattern(query_string, fields := NULL, top_k := 50, k := 1.2, b := 0.75, term_limit := 32, max_df_ratio := 0.15, max_df := 50000, enable_prefix := true, enable_substring := true, enable_fuzzy := true, enable_short_fuzzy := true, expand_exact_terms := false, query_mode := 'standard', field_weights := NULL, field_b := NULL, scoring_model := 'bm25f', tie_breaker := 0.0, near_distance := 10) AS TABLE
WITH params(term_limit, max_df_ratio, max_df, enable_prefix, enable_substring, enable_fuzzy, enable_short_fuzzy, expand_exact_terms, query_mode, near_distance, field_weights, field_b, scoring_model, tie_breaker, default_b) AS (
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
               WHEN 'phrase' THEN 'phrase'
               WHEN 'phrase_prefix' THEN 'phrase_prefix'
               WHEN 'near' THEN 'near'
               WHEN 'wildcard' THEN 'wildcard'
               WHEN 'regex' THEN 'regex'
               WHEN 'exact' THEN 'exact'
               WHEN 'prefix' THEN 'prefix'
               ELSE error('query_mode must be one of standard, exact, prefix, autocomplete, phrase, phrase_prefix, near, wildcard, or regex')
           END,
           try_cast(near_distance AS BIGINT),
           field_weights::MAP(VARCHAR, DOUBLE),
           field_b::MAP(VARCHAR, DOUBLE),
           lower(scoring_model::VARCHAR),
           tie_breaker::DOUBLE,
           b::DOUBLE
),
{{field_scoring_config_ctes}}
search_validation_errors AS (
    SELECT message
    FROM (
        SELECT 10 AS priority,
               'query_mode must be one of standard, exact, prefix, autocomplete, phrase, phrase_prefix, near, wildcard, or regex' AS message
        WHERE query_mode IS NULL
           OR lower(query_mode::VARCHAR) NOT IN (
               'standard',
               'exact',
               'prefix',
               'autocomplete',
               'phrase',
               'phrase_prefix',
               'near',
               'wildcard',
               'regex'
           )
        UNION ALL
        SELECT 20 AS priority,
               'near_distance must be a non-negative integer' AS message
        WHERE lower(coalesce(query_mode::VARCHAR, '')) = 'near'
          AND (
              near_distance IS NULL
              OR typeof(near_distance) IN ('VARCHAR', 'BOOLEAN')
              OR try_cast(near_distance AS HUGEINT) IS NULL
              OR try_cast(near_distance AS HUGEINT) < 0
              OR try_cast(near_distance AS HUGEINT) > 9223372036854775807
              -- Compared in the argument's own type: a DOUBLE round trip cannot
              -- distinguish 2.0000000000000000001 from an exact 2. Strings hold
              -- their fraction only through the DOUBLE comparison.
              OR try_cast(near_distance AS HUGEINT) <> near_distance
              OR try_cast(near_distance AS DOUBLE)
                 <> floor(try_cast(near_distance AS DOUBLE))
          )
        UNION ALL
        SELECT 30 AS priority,
               message
        FROM validation_errors
    ) AS raw_search_validation_errors
    ORDER BY priority,
             message
    LIMIT 1
),
df_cap(max_df) AS (
    SELECT least(params.max_df, ceil(stats.num_docs * params.max_df_ratio)::BIGINT)
    FROM {{fts_schema}}.stats AS stats
    CROSS JOIN params
),
tokenized AS (
    SELECT unnest(tokens) AS raw_term,
           generate_subscripts(tokens, 1)::BIGINT AS token_position{{is_final_token}}
    FROM (
        SELECT list_filter(
                   {{fts_schema}}.tokenize(query_string),
                   lambda token: token IS NOT NULL AND token <> ''
               ) AS tokens
    ) AS query_token_list
),
query_analyzer_tokens AS (
    SELECT analyzed.raw_term AS raw_token,
           analyzed.term,
           analyzed.token_position
    FROM (
        {{analyzed_tokens}}
    ) AS analyzed
    {{phrase_prefix_stopword_token}}
),
query_shape AS (
    SELECT count(*)::BIGINT AS token_count,
           max(token_position) AS final_position,
           CASE
               WHEN params.query_mode = 'phrase'
                AND count(*) = 1
                   THEN 'exact'
               WHEN params.query_mode = 'phrase_prefix'
                AND count(*) = 1
                   THEN 'prefix'
               WHEN params.query_mode = 'near'
                AND count(DISTINCT term) = 1
                   THEN 'standard'
               ELSE params.query_mode
           END AS effective_mode
    FROM query_analyzer_tokens
    CROSS JOIN params
    GROUP BY params.query_mode
),
leaf_shape_errors AS (
    SELECT 'query_mode ' || params.query_mode || ' requires exactly one query token' AS message
    FROM query_shape
    CROSS JOIN params
    WHERE params.query_mode IN ('exact', 'prefix')
      AND query_shape.token_count <> 1
),
autocomplete_final_token AS (
    SELECT raw_token AS query_term,
           length(raw_token)::BIGINT AS query_len,
           least(length(raw_token), 3)::UTINYINT AS prefix_len,
           substr(raw_token, 1, least(length(raw_token), 3)) AS prefix
    FROM query_analyzer_tokens
    CROSS JOIN query_shape
    WHERE query_shape.effective_mode = 'autocomplete'
      AND token_position = query_shape.final_position
      AND length(raw_token) >= 2
),
stemmed_tokens AS (
    SELECT DISTINCT query_term
    FROM (
        SELECT term AS query_term
        FROM query_analyzer_tokens
        CROSS JOIN query_shape
        WHERE query_shape.effective_mode IN ('standard', 'exact')
           OR (
               query_shape.effective_mode = 'autocomplete'
               AND token_position < query_shape.final_position
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
    CROSS JOIN query_shape
    CROSS JOIN params
    WHERE query_shape.effective_mode = 'standard'
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
prefix_input AS (
    SELECT raw_token AS query_term,
           length(raw_token)::BIGINT AS query_len,
           least(length(raw_token), 3)::UTINYINT AS prefix_len,
           substr(raw_token, 1, least(length(raw_token), 3)) AS prefix
    FROM query_analyzer_tokens
    CROSS JOIN query_shape
    WHERE query_shape.effective_mode = 'prefix'
),
prefix_candidates AS (
    -- No DF cap or term_limit, unlike autocomplete. Length two or more uses the
    -- prefix sidecar; length one has no sidecar entry (2- and 3-char prefixes only)
    -- and scans raw_dict.
    SELECT prefix_input.query_term,
           raw_dict.termid,
           raw_dict.rawtermid,
           raw_dict.raw_term AS term,
           raw_dict.df,
           1.0::DOUBLE AS expansion_weight,
           'prefix' AS match_type
    FROM prefix_input
    JOIN {{fts_schema}}.term_prefixes AS term_prefixes
      ON term_prefixes.prefix_len = prefix_input.prefix_len
     AND term_prefixes.prefix = prefix_input.prefix
    JOIN {{fts_schema}}.raw_dict AS raw_dict
      ON raw_dict.rawtermid = term_prefixes.rawtermid
    WHERE prefix_input.query_len >= 2
      AND starts_with(raw_dict.raw_term, prefix_input.query_term)
    UNION ALL
    SELECT prefix_input.query_term,
           raw_dict.termid,
           raw_dict.rawtermid,
           raw_dict.raw_term AS term,
           raw_dict.df,
           1.0::DOUBLE AS expansion_weight,
           'prefix' AS match_type
    FROM prefix_input
    JOIN {{fts_schema}}.raw_dict AS raw_dict
      ON starts_with(raw_dict.raw_term, prefix_input.query_term)
    WHERE prefix_input.query_len = 1
),
phrase_tokens AS (
    SELECT raw_token,
           term,
           token_position,
           token_position - 1 AS relative_position,
           token_position AS phrase_slot
    FROM query_analyzer_tokens
    CROSS JOIN query_shape
    WHERE query_shape.effective_mode IN ('phrase', 'phrase_prefix')
),
phrase_exact_slots AS (
    SELECT phrase_tokens.phrase_slot,
           phrase_tokens.relative_position,
           term_stats.termid,
           term_stats.df
    FROM phrase_tokens
    JOIN {{fts_schema}}.term_stats AS term_stats
      ON term_stats.term = phrase_tokens.term
    CROSS JOIN query_shape
    WHERE query_shape.effective_mode = 'phrase'
       OR phrase_tokens.token_position < query_shape.final_position
),
phrase_prefix_input AS (
    SELECT phrase_tokens.raw_token AS query_term,
           least(length(phrase_tokens.raw_token), 3)::UTINYINT AS prefix_len,
           substr(
               phrase_tokens.raw_token,
               1,
               least(length(phrase_tokens.raw_token), 3)
           ) AS prefix,
           phrase_tokens.relative_position
    FROM phrase_tokens
    CROSS JOIN query_shape
    WHERE query_shape.effective_mode = 'phrase_prefix'
      AND phrase_tokens.token_position = query_shape.final_position
      AND length(phrase_tokens.raw_token) >= 2
),
phrase_prefix_candidates AS (
    SELECT raw_dict.termid,
           raw_dict.rawtermid,
           prefix_input.relative_position,
           row_number() OVER (
               ORDER BY (raw_dict.raw_term = prefix_input.query_term) DESC,
                        raw_dict.df ASC,
                        length(raw_dict.raw_term) ASC,
                        raw_dict.raw_term ASC,
                        raw_dict.rawtermid ASC
           ) AS expansion_rank
    FROM phrase_prefix_input AS prefix_input
    JOIN {{fts_schema}}.term_prefixes AS term_prefixes
      ON term_prefixes.prefix_len = prefix_input.prefix_len
     AND term_prefixes.prefix = prefix_input.prefix
    JOIN {{fts_schema}}.raw_dict AS raw_dict
      ON raw_dict.rawtermid = term_prefixes.rawtermid
    WHERE starts_with(raw_dict.raw_term, prefix_input.query_term)
),
selected_phrase_prefixes AS (
    SELECT termid,
           rawtermid,
           relative_position
    FROM phrase_prefix_candidates
    CROSS JOIN params
    WHERE expansion_rank <= params.term_limit
),
phrase_anchor AS (
    SELECT phrase_slot,
           relative_position,
           termid
    FROM phrase_exact_slots
    ORDER BY df ASC,
             phrase_slot ASC,
             termid ASC
    LIMIT 1
),
phrase_candidate_starts AS (
    SELECT terms.docid,
           terms.fieldid,
           terms.position::BIGINT - phrase_anchor.relative_position AS phrase_start
    FROM phrase_anchor
    JOIN {{fts_schema}}.terms AS terms
      ON terms.termid = phrase_anchor.termid
    WHERE terms.fieldid IN (SELECT fieldid FROM field_config)
),
phrase_exact_matches AS (
    SELECT candidates.docid,
           candidates.fieldid,
           candidates.phrase_start
    FROM phrase_candidate_starts AS candidates
    JOIN phrase_exact_slots AS slots ON true
    JOIN {{fts_schema}}.terms AS terms
      ON terms.docid = candidates.docid
     AND terms.fieldid = candidates.fieldid
     AND terms.termid = slots.termid
     AND terms.position::BIGINT = candidates.phrase_start + slots.relative_position
    CROSS JOIN query_shape
    GROUP BY candidates.docid,
             candidates.fieldid,
             candidates.phrase_start,
             query_shape.effective_mode,
             query_shape.token_count
    HAVING count(DISTINCT slots.phrase_slot) = CASE
        WHEN query_shape.effective_mode = 'phrase_prefix'
            THEN query_shape.token_count - 1
        ELSE query_shape.token_count
    END
),
phrase_prefix_postings AS (
    SELECT terms.docid,
           terms.fieldid,
           terms.position,
           prefixes.relative_position
    FROM selected_phrase_prefixes AS prefixes
    JOIN {{fts_schema}}.terms AS terms
      ON terms.termid = prefixes.termid
     AND terms.rawtermid = prefixes.rawtermid
    WHERE terms.fieldid IN (SELECT fieldid FROM field_config)
),
phrase_matches AS (
    SELECT phrase_exact_matches.*
    FROM phrase_exact_matches
    CROSS JOIN query_shape
    WHERE lower(query_mode::VARCHAR) = 'phrase'
      AND query_shape.effective_mode = 'phrase'
    UNION ALL
    SELECT DISTINCT exact_matches.docid,
                    exact_matches.fieldid,
                    exact_matches.phrase_start
    FROM phrase_exact_matches AS exact_matches
    JOIN phrase_prefix_postings AS prefix_postings
      ON prefix_postings.docid = exact_matches.docid
     AND prefix_postings.fieldid = exact_matches.fieldid
     AND prefix_postings.position::BIGINT
         = exact_matches.phrase_start + prefix_postings.relative_position
    CROSS JOIN query_shape
    WHERE lower(query_mode::VARCHAR) = 'phrase_prefix'
      AND query_shape.effective_mode = 'phrase_prefix'
),
phrase_idf AS (
    SELECT sum(
               log(((((stats.num_docs - slots.df) + 0.5) / (slots.df + 0.5)) + 1))
           ) AS phrase_idf
    FROM phrase_exact_slots AS slots
    CROSS JOIN {{fts_schema}}.stats AS stats
),
phrase_field_term_tf AS (
    SELECT NULL::BIGINT AS termid,
           NULL::BIGINT AS rawtermid,
           phrase_matches.docid,
           phrase_matches.fieldid,
           any_value(phrase_idf.phrase_idf) AS idf,
           1.0::DOUBLE AS expansion_weight,
           count(*)::DOUBLE AS tf
    FROM phrase_matches
    CROSS JOIN phrase_idf
    WHERE phrase_idf.phrase_idf IS NOT NULL
    GROUP BY phrase_matches.docid,
             phrase_matches.fieldid
),
fts_extension_autoload AS (
    -- Bind DuckDB's stable FTS autoload entry before the near scan.
    SELECT stem('', 'none') AS marker
),
near_slots AS (
    -- One slot per distinct query term, so a repeated term adds its IDF once.
    SELECT min(query_analyzer_tokens.token_position) AS slot,
           term_stats.termid,
           any_value(term_stats.df) AS df
    FROM query_analyzer_tokens
    JOIN {{fts_schema}}.term_stats AS term_stats
      ON term_stats.term = query_analyzer_tokens.term
    CROSS JOIN query_shape
    WHERE query_shape.effective_mode = 'near'
    GROUP BY term_stats.termid
),
near_slot_count AS (
    -- Counted over query terms, not found ones: an absent term must leave a slot no posting can fill.
    SELECT count(DISTINCT query_analyzer_tokens.term)::BIGINT AS slot_count
    FROM query_analyzer_tokens
    CROSS JOIN query_shape
    WHERE query_shape.effective_mode = 'near'
),
near_window AS (
    -- near_distance excludes the first and last term, so the span is near_distance + 1; the cap keeps it inside INT64.
    SELECT least(greatest(params.near_distance, 0), 4611686018427387903) + 1 AS width
    FROM params
),
near_anchor AS (
    SELECT termid
    FROM near_slots
    WHERE (SELECT count(*) FROM near_slots)
        = (SELECT slot_count FROM near_slot_count)
    ORDER BY df ASC,
             slot ASC,
             termid ASC
    LIMIT 1
),
near_candidate_fields AS (
    SELECT DISTINCT terms.docid,
                    terms.fieldid
    FROM near_anchor
    JOIN {{fts_schema}}.terms AS terms
      ON terms.termid = near_anchor.termid
    WHERE terms.fieldid IN (SELECT fieldid FROM field_config)
),
near_grouped AS (
    SELECT terms.docid,
           terms.fieldid,
           list(terms.position::BIGINT) AS positions,
           list(near_slots.termid) AS termids
    FROM near_candidate_fields AS candidates
    JOIN {{fts_schema}}.terms AS terms
      ON terms.docid = candidates.docid
     AND terms.fieldid = candidates.fieldid
    JOIN near_slots
      ON near_slots.termid = terms.termid
    GROUP BY terms.docid,
             terms.fieldid
),
near_idf AS (
    SELECT sum(
               log(((((stats.num_docs - near_slots.df) + 0.5) / (near_slots.df + 0.5)) + 1))
           ) AS near_idf
    FROM near_slots
    CROSS JOIN {{fts_schema}}.stats AS stats
),
near_field_term_tf AS (
    SELECT scored.termid,
           scored.rawtermid,
           scored.docid,
           scored.fieldid,
           scored.idf,
           scored.expansion_weight,
           scored.tf
    FROM (
        SELECT NULL::BIGINT AS termid,
               NULL::BIGINT AS rawtermid,
               near_grouped.docid,
               near_grouped.fieldid,
               near_idf.near_idf AS idf,
               1.0::DOUBLE AS expansion_weight,
               fts_near_tf(
                   near_grouped.positions,
                   near_grouped.termids,
                   near_slot_count.slot_count,
                   near_window.width
               )::DOUBLE AS tf
        FROM near_grouped
        CROSS JOIN fts_extension_autoload
        CROSS JOIN near_slot_count
        CROSS JOIN near_window
        CROSS JOIN near_idf
        WHERE near_idf.near_idf IS NOT NULL
    ) AS scored
    WHERE scored.tf > 0
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
        UNION ALL
        SELECT *
        FROM prefix_candidates
    ) AS terms
    GROUP BY query_term,
             termid,
             rawtermid
),
standard_field_term_tf AS (
    SELECT selected_terms.termid,
           selected_terms.rawtermid,
           terms.docid,
           terms.fieldid,
           any_value(
               log(
                   ((((stats.num_docs - selected_terms.df) + 0.5)
                       / (selected_terms.df + 0.5)) + 1)
               )
           ) AS idf,
           max(selected_terms.expansion_weight) AS expansion_weight,
           count(*) AS tf
    FROM selected_terms
    JOIN {{fts_schema}}.terms AS terms
      ON terms.termid = selected_terms.termid
     AND (
         selected_terms.rawtermid IS NULL
         OR terms.rawtermid = selected_terms.rawtermid
     )
    CROSS JOIN {{fts_schema}}.stats AS stats
    WHERE terms.fieldid IN (SELECT fieldid FROM field_config)
    GROUP BY selected_terms.termid,
             selected_terms.rawtermid,
             terms.docid,
             terms.fieldid
),
field_term_tf AS (
    SELECT *
    FROM standard_field_term_tf
    UNION ALL
    SELECT *
    FROM phrase_field_term_tf
    UNION ALL
    SELECT *
    FROM near_field_term_tf
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
-- Every column carries the error: a pushed predicate on a constant column
-- would fold to false and drop this branch before its filter runs.
SELECT CASE WHEN error(message) THEN NULL::VARCHAR END AS docname,
       CASE WHEN error(message) THEN NULL::DOUBLE END AS score,
       CASE WHEN error(message) THEN NULL::BIGINT END AS rank
FROM search_validation_errors
WHERE error(message)
UNION ALL
SELECT CASE WHEN error(message) THEN NULL::VARCHAR END AS docname,
       CASE WHEN error(message) THEN NULL::DOUBLE END AS score,
       CASE WHEN error(message) THEN NULL::BIGINT END AS rank
FROM leaf_shape_errors
WHERE error(message)
ORDER BY rank;
