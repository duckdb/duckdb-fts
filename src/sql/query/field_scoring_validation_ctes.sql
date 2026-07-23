weight_entries AS (
    SELECT unnest(map_entries({{params}}.{{field_weights}}), recursive := true)
    FROM {{params}}
),
field_b_entries AS (
    SELECT unnest(map_entries({{params}}.{{field_b}}), recursive := true)
    FROM {{params}}
),
raw_field_scoring_validation_errors AS (
    SELECT 10 AS priority,
           'scoring_model must be either bm25f or best_fields' AS message
    FROM {{params}}
    WHERE {{scoring_model}} IS NULL
       OR {{scoring_model}} NOT IN ('bm25f', 'best_fields')
    UNION ALL
    SELECT 20 AS priority,
           'tie_breaker must be finite and between 0 and 1' AS message
    FROM {{params}}
    WHERE {{tie_breaker}} IS NULL
       OR NOT isfinite({{tie_breaker}})
       OR {{tie_breaker}} NOT BETWEEN 0.0 AND 1.0
    UNION ALL
    SELECT 30 AS priority,
           'tie_breaker is only supported with best_fields scoring' AS message
    FROM {{params}}
    WHERE {{scoring_model}} = 'bm25f'
      AND {{tie_breaker}} <> 0.0
    UNION ALL
    SELECT 40 AS priority,
           'b must be finite and between 0 and 1' AS message
    FROM {{params}}
    WHERE {{default_b}} IS NULL
       OR NOT isfinite({{default_b}})
       OR {{default_b}} NOT BETWEEN 0.0 AND 1.0
    UNION ALL
    SELECT 50 AS priority,
           'field weight for ' || key || ' must be finite and non-negative' AS message
    FROM weight_entries
    WHERE value IS NULL
       OR NOT isfinite(value)
       OR value < 0.0
    UNION ALL
    SELECT 60 AS priority,
           'field_weights contains unknown field: ' || key AS message
    FROM weight_entries
    WHERE key NOT IN (SELECT field FROM {{fts_schema}}.fields)
    UNION ALL
    SELECT 70 AS priority,
           'field b for ' || key || ' must be finite and between 0 and 1' AS message
    FROM field_b_entries
    WHERE value IS NULL
       OR NOT isfinite(value)
       OR value NOT BETWEEN 0.0 AND 1.0
    UNION ALL
    SELECT 80 AS priority,
           'field_b contains unknown field: ' || key AS message
    FROM field_b_entries
    WHERE key NOT IN (SELECT field FROM {{fts_schema}}.fields)
),
validation_errors AS (
    SELECT message
    FROM raw_field_scoring_validation_errors
    ORDER BY priority,
             message
    LIMIT 1
),
