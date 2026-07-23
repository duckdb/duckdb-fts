		CREATE OR REPLACE MACRO {{fts_schema}}.match_layered_bm25_query(input_id, query, k := 1.2, b := 0.75, term_limit := 32, max_df_ratio := 0.15, max_df := 50000, enable_prefix := true, enable_substring := true, enable_fuzzy := true, enable_short_fuzzy := true, expand_exact_terms := false, field_weights := NULL, field_b := NULL, scoring_model := 'bm25f', tie_breaker := 0.0, max_leaf_clauses := 1024, max_boolean_depth := 64) AS (
          SELECT score
          FROM {{fts_schema}}.search_layered_bm25_query(
              query,
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
              field_weights := field_weights,
              field_b := field_b,
              scoring_model := scoring_model,
              tie_breaker := tie_breaker,
              max_leaf_clauses := max_leaf_clauses,
              max_boolean_depth := max_boolean_depth
          ) AS hits
          WHERE hits.docname = input_id
      );
