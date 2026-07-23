CREATE OR REPLACE MACRO {{fts_schema}}.search_layered_bm25_query(query, top_k := 50, k := 1.2, b := 0.75, term_limit := 32, max_df_ratio := 0.15, max_df := 50000, enable_prefix := true, enable_substring := true, enable_fuzzy := true, enable_short_fuzzy := true, expand_exact_terms := false, field_weights := NULL, field_b := NULL, scoring_model := 'bm25f', tie_breaker := 0.0, max_leaf_clauses := 1024, max_boolean_depth := 64) AS TABLE
WITH RECURSIVE query_params AS (
    SELECT query::JSON AS query_json,
           top_k::BIGINT AS result_limit,
           k::DOUBLE AS bm25_k,
           b::DOUBLE AS bm25_b,
           term_limit::BIGINT AS expansion_term_limit,
           max_df_ratio::DOUBLE AS expansion_max_df_ratio,
           max_df::BIGINT AS expansion_max_df,
           enable_prefix::BOOLEAN AS use_prefix,
           enable_substring::BOOLEAN AS use_substring,
           enable_fuzzy::BOOLEAN AS use_fuzzy,
           enable_short_fuzzy::BOOLEAN AS use_short_fuzzy,
           expand_exact_terms::BOOLEAN AS use_exact_expansion,
           field_weights::MAP(VARCHAR, DOUBLE) AS scoring_weights,
           field_b::MAP(VARCHAR, DOUBLE) AS scoring_field_b,
           lower(scoring_model::VARCHAR) AS scoring_model,
           tie_breaker::DOUBLE AS scoring_tie_breaker,
           max_leaf_clauses IS NULL AS leaf_limit_disabled,
           try_cast(max_leaf_clauses AS DOUBLE) AS leaf_limit_number,
           try_cast(max_leaf_clauses AS BIGINT) AS leaf_clause_limit,
           max_boolean_depth IS NULL AS depth_limit_disabled,
           try_cast(max_boolean_depth AS DOUBLE) AS depth_limit_number,
           try_cast(max_boolean_depth AS BIGINT) AS boolean_depth_limit
),
{{field_scoring_validation_ctes}}
json_tree_rows AS (
    SELECT tree.*
    FROM query_params
    CROSS JOIN LATERAL json_tree(query_params.query_json) AS tree
),
occurrence_arrays AS (
    SELECT id,
           parent,
           key AS occurrence
    FROM json_tree_rows
    WHERE key IN ('must', 'should', 'must_not')
      AND type = 'ARRAY'
),
query_nodes AS (
    SELECT tree.id AS node_id,
           arrays.parent AS parent_id,
           arrays.occurrence,
           length(tree.fullkey) - length(replace(tree.fullkey, '[', '')) AS depth,
           tree.value AS node_json,
           list_contains(json_keys(tree.value), 'query') AS has_query,
           list_has_any(json_keys(tree.value), ['must', 'should', 'must_not']) AS has_boolean
    FROM json_tree_rows AS tree
    LEFT JOIN occurrence_arrays AS arrays ON arrays.id = tree.parent
    WHERE tree.type = 'OBJECT'
      AND (tree.parent IS NULL OR arrays.id IS NOT NULL)
),
node_edges AS (
    SELECT parent_id,
           node_id AS child_id,
           occurrence
    FROM query_nodes
    WHERE parent_id IS NOT NULL
),
node_keys AS (
    SELECT nodes.node_id,
           nodes.has_query,
           nodes.has_boolean,
           unnest(json_keys(nodes.node_json)) AS node_key
    FROM query_nodes AS nodes
),
field_entries AS (
    SELECT nodes.node_id,
           entry.key AS ordinal,
           entry.type AS field_type,
           json_extract_string(entry.value, '$') AS field_name
    FROM query_nodes AS nodes
    CROSS JOIN LATERAL json_each(
        CASE
            WHEN json_type(json_extract(nodes.node_json, '$.fields')) = 'ARRAY'
                THEN json_extract(nodes.node_json, '$.fields')
            ELSE '[]'::JSON
        END
    ) AS entry
    WHERE nodes.has_query
),
field_lists AS (
    SELECT node_id,
           string_agg(field_name, ',' ORDER BY ordinal::UBIGINT) AS fields
    FROM field_entries
    GROUP BY node_id
),
node_config AS (
    SELECT nodes.*,
           CASE
               WHEN list_contains(json_keys(node_json), 'boost')
                   THEN coalesce(try_cast(json_extract(node_json, '$.boost') AS DOUBLE), 1.0)
               ELSE 1.0
           END AS boost
    FROM query_nodes AS nodes
),
leaves AS (
    SELECT node_id,
           depth,
           json_extract_string(node_json, '$.query') AS query_string,
           CASE
               WHEN list_contains(json_keys(node_json), 'query_mode')
                   THEN coalesce(json_extract_string(node_json, '$.query_mode'), 'standard')
               ELSE 'standard'
           END AS query_mode,
           boost
    FROM node_config
    WHERE has_query
),
group_counts AS (
    SELECT nodes.node_id,
           count(edges.child_id) FILTER (WHERE edges.occurrence = 'must') AS must_count,
           count(edges.child_id) FILTER (WHERE edges.occurrence = 'should') AS should_count,
           count(edges.child_id) FILTER (WHERE edges.occurrence = 'must_not') AS must_not_count
    FROM node_config AS nodes
    LEFT JOIN node_edges AS edges ON edges.parent_id = nodes.node_id
    WHERE NOT nodes.has_query
    GROUP BY nodes.node_id
),
group_metadata AS (
    SELECT nodes.node_id,
           nodes.depth,
           nodes.boost,
           counts.must_count,
           counts.should_count,
           counts.must_not_count,
           CASE
               WHEN list_contains(json_keys(nodes.node_json), 'minimum_should_match')
                   THEN coalesce(try_cast(json_extract(nodes.node_json, '$.minimum_should_match') AS UBIGINT), 0)
               WHEN counts.must_count = 0 AND counts.should_count > 0 THEN 1
               ELSE 0
           END AS minimum_should_match
    FROM node_config AS nodes
    JOIN group_counts AS counts USING (node_id)
    WHERE NOT nodes.has_query
),
raw_query_validation_errors AS (
    SELECT 'max_leaf_clauses must be NULL or a positive integer' AS message
    FROM query_params
    WHERE NOT leaf_limit_disabled
      AND (
          leaf_limit_number IS NULL
          OR NOT isfinite(leaf_limit_number)
          OR leaf_clause_limit IS NULL
          OR leaf_limit_number <> leaf_clause_limit::DOUBLE
          OR leaf_clause_limit < 1
      )
    UNION ALL
    SELECT 'max_boolean_depth must be NULL or a positive integer' AS message
    FROM query_params
    WHERE NOT depth_limit_disabled
      AND (
          depth_limit_number IS NULL
          OR NOT isfinite(depth_limit_number)
          OR boolean_depth_limit IS NULL
          OR depth_limit_number <> boolean_depth_limit::DOUBLE
          OR boolean_depth_limit < 1
      )
    UNION ALL
    SELECT 'query must be a non-null JSON object' AS message
    FROM query_params
    WHERE query_json IS NULL OR json_type(query_json) <> 'OBJECT'
    UNION ALL
    SELECT 'query node cannot contain both query and Boolean clauses' AS message
    FROM query_nodes
    WHERE has_query AND has_boolean
    UNION ALL
    SELECT 'query leaf must contain a string query' AS message
    FROM query_nodes
    WHERE has_query
      AND json_type(json_extract(node_json, '$.query')) <> 'VARCHAR'
    UNION ALL
    SELECT 'query node contains unknown key: ' || node_key AS message
    FROM node_keys
    WHERE (has_query AND NOT has_boolean AND node_key NOT IN ('query', 'fields', 'query_mode', 'boost'))
       OR (NOT has_query AND node_key NOT IN ('must', 'should', 'must_not', 'minimum_should_match', 'boost'))
    UNION ALL
    SELECT 'query node contains duplicate keys' AS message
    FROM query_nodes
    WHERE list_count(json_keys(node_json)) <> list_count(list_distinct(json_keys(node_json)))
    UNION ALL
    SELECT key || ' must be an array' AS message
    FROM json_tree_rows
    WHERE key IN ('must', 'should', 'must_not')
      AND type <> 'ARRAY'
    UNION ALL
    SELECT arrays.occurrence || ' entries must be JSON objects' AS message
    FROM occurrence_arrays AS arrays
    JOIN json_tree_rows AS children ON children.parent = arrays.id
    WHERE children.type <> 'OBJECT'
    UNION ALL
    SELECT 'fields must be a non-empty array of field names' AS message
    FROM query_nodes
    WHERE has_query
      AND list_contains(json_keys(node_json), 'fields')
      AND (
          json_type(json_extract(node_json, '$.fields')) <> 'ARRAY'
          OR json_array_length(json_extract(node_json, '$.fields')) = 0
      )
    UNION ALL
    SELECT 'fields entries must be non-empty strings without commas or surrounding whitespace' AS message
    FROM field_entries
    WHERE field_type <> 'VARCHAR'
       OR field_name IS NULL
       OR field_name = ''
       OR field_name <> trim(field_name)
       OR contains(field_name, ',')
    UNION ALL
    SELECT 'fields contains unknown field: ' || field_name AS message
    FROM field_entries
    WHERE field_type = 'VARCHAR'
      AND field_name NOT IN (SELECT field FROM {{fts_schema}}.fields)
    UNION ALL
    SELECT 'query_mode must be either standard or autocomplete' AS message
    FROM query_nodes
    WHERE has_query
      AND list_contains(json_keys(node_json), 'query_mode')
      AND (
          json_type(json_extract(node_json, '$.query_mode')) <> 'VARCHAR'
          OR lower(json_extract_string(node_json, '$.query_mode')) NOT IN ('standard', 'autocomplete')
      )
    UNION ALL
    SELECT 'boost must be finite and non-negative' AS message
    FROM query_nodes
    WHERE list_contains(json_keys(node_json), 'boost')
      AND (
          json_type(json_extract(node_json, '$.boost')) NOT IN ('UBIGINT', 'BIGINT', 'DOUBLE')
          OR try_cast(json_extract(node_json, '$.boost') AS DOUBLE) IS NULL
          OR NOT isfinite(try_cast(json_extract(node_json, '$.boost') AS DOUBLE))
          OR try_cast(json_extract(node_json, '$.boost') AS DOUBLE) < 0.0
      )
    UNION ALL
    SELECT 'Boolean group must contain at least one must or should clause' AS message
    FROM group_counts
    WHERE must_count + should_count = 0
    UNION ALL
    SELECT 'minimum_should_match must be a non-negative integer no greater than the number of should clauses' AS message
    FROM query_nodes AS nodes
    LEFT JOIN group_counts AS counts USING (node_id)
    WHERE NOT nodes.has_query
      AND list_contains(json_keys(nodes.node_json), 'minimum_should_match')
      AND (
          json_type(json_extract(nodes.node_json, '$.minimum_should_match')) NOT IN ('UBIGINT', 'BIGINT')
          OR try_cast(json_extract(nodes.node_json, '$.minimum_should_match') AS UBIGINT) IS NULL
          OR try_cast(json_extract(nodes.node_json, '$.minimum_should_match') AS UBIGINT) > counts.should_count
      )
    UNION ALL
    SELECT 'query exceeds max_leaf_clauses of ' || leaf_clause_limit AS message
    FROM query_nodes
    CROSS JOIN query_params
    WHERE NOT leaf_limit_disabled
      AND leaf_clause_limit IS NOT NULL
    GROUP BY leaf_clause_limit
    HAVING count(*) FILTER (WHERE has_query) > leaf_clause_limit
    UNION ALL
    SELECT 'query exceeds max_boolean_depth of ' || boolean_depth_limit AS message
    FROM query_nodes
    CROSS JOIN query_params
    WHERE NOT depth_limit_disabled
      AND boolean_depth_limit IS NOT NULL
    GROUP BY boolean_depth_limit
    HAVING max(depth) > boolean_depth_limit
),
query_validation_errors AS (
    SELECT message
    FROM raw_query_validation_errors
    ORDER BY CASE
        WHEN message LIKE 'max_% must %' THEN 5
        WHEN message = 'query must be a non-null JSON object' THEN 10
        WHEN message = 'query node cannot contain both query and Boolean clauses' THEN 20
        WHEN message LIKE '% must be an array'
          OR message LIKE '% entries must be JSON objects' THEN 30
        WHEN message = 'query node contains duplicate keys' THEN 40
        WHEN message LIKE 'query node contains unknown key:%' THEN 50
        WHEN message = 'query leaf must contain a string query' THEN 60
        WHEN message LIKE 'fields entries %'
          OR message LIKE 'fields must %' THEN 70
        WHEN message LIKE 'fields contains %' THEN 71
        WHEN message LIKE 'query_mode %' THEN 80
        WHEN message LIKE 'boost %' THEN 90
        WHEN message LIKE 'Boolean group %' THEN 100
        WHEN message LIKE 'minimum_should_match %' THEN 110
        WHEN message LIKE 'query exceeds max_leaf_clauses %' THEN 120
        WHEN message LIKE 'query exceeds max_boolean_depth %' THEN 121
        ELSE 130
    END,
    message
    LIMIT 1
),
selected_validation_error AS (
    SELECT message
    FROM (
        SELECT 10 AS priority, message FROM validation_errors
        UNION ALL
        SELECT 20 AS priority, message FROM query_validation_errors
    ) AS errors
    ORDER BY priority, message
    LIMIT 1
),
leaf_scores AS (
    SELECT leaves.node_id,
           leaves.depth,
           hits.docname,
           leaves.boost * hits.score AS score
    FROM leaves
    LEFT JOIN field_lists USING (node_id)
    CROSS JOIN query_params
    CROSS JOIN LATERAL {{fts_schema}}.search_layered_bm25(
        leaves.query_string,
        fields := field_lists.fields,
        top_k := NULL::BIGINT,
        k := query_params.bm25_k,
        b := query_params.bm25_b,
        term_limit := query_params.expansion_term_limit,
        max_df_ratio := query_params.expansion_max_df_ratio,
        max_df := query_params.expansion_max_df,
        enable_prefix := query_params.use_prefix,
        enable_substring := query_params.use_substring,
        enable_fuzzy := query_params.use_fuzzy,
        enable_short_fuzzy := query_params.use_short_fuzzy,
        expand_exact_terms := query_params.use_exact_expansion,
        query_mode := leaves.query_mode,
        field_weights := query_params.scoring_weights,
        field_b := query_params.scoring_field_b,
        scoring_model := query_params.scoring_model,
        tie_breaker := query_params.scoring_tie_breaker
    ) AS hits
    WHERE NOT EXISTS (SELECT 1 FROM selected_validation_error)
),
initial_node_results(node_id, docname, score, matched) AS (
    SELECT node_id,
           docname,
           score,
           true AS matched
    FROM leaf_scores
    UNION ALL
    SELECT groups.node_id,
           docs.name AS docname,
           0.0::DOUBLE AS score,
           true AS matched
    FROM group_metadata AS groups
    CROSS JOIN {{fts_schema}}.docs AS docs
    WHERE groups.must_count = 0
      AND groups.minimum_should_match = 0
      AND NOT EXISTS (SELECT 1 FROM selected_validation_error)
),
node_results(node_id, docname, score, matched)
USING KEY (node_id, docname) AS (
    SELECT node_id,
           docname,
           score,
           matched
    FROM initial_node_results
    UNION ALL
    SELECT evaluated.node_id,
           evaluated.docname,
           CASE
               WHEN evaluated.matched
                   THEN evaluated.boost * evaluated.positive_score
               ELSE 0.0
           END AS score,
           evaluated.matched
    FROM (
        SELECT groups.node_id,
               candidates.docname,
               groups.boost,
               coalesce(sum(child_results.score) FILTER (
                   WHERE child_edges.occurrence IN ('must', 'should')
                     AND child_results.matched
               ), 0.0) AS positive_score,
               count(DISTINCT child_edges.child_id) FILTER (
                   WHERE child_edges.occurrence = 'must'
                     AND child_results.matched
               ) = groups.must_count
               AND count(DISTINCT child_edges.child_id) FILTER (
                   WHERE child_edges.occurrence = 'should'
                     AND child_results.matched
               ) >= groups.minimum_should_match
               AND count(DISTINCT child_edges.child_id) FILTER (
                   WHERE child_edges.occurrence = 'must_not'
                     AND child_results.matched
               ) = 0 AS matched
        FROM (
            SELECT DISTINCT child_edges.parent_id AS node_id,
                            changed_results.docname
            FROM node_results AS changed_results
            JOIN node_edges AS child_edges
              ON child_edges.child_id = changed_results.node_id
        ) AS candidates
        JOIN group_metadata AS groups USING (node_id)
        LEFT JOIN node_edges AS child_edges
          ON child_edges.parent_id = groups.node_id
        LEFT JOIN recurring.node_results AS child_results
          ON child_results.node_id = child_edges.child_id
         AND child_results.docname = candidates.docname
        GROUP BY groups.node_id,
                 candidates.docname,
                 groups.boost,
                 groups.must_count,
                 groups.minimum_should_match
    ) AS evaluated
),
ranked AS (
    SELECT docname,
           score,
           row_number() OVER (ORDER BY score DESC, docname) AS rank
    FROM node_results
    WHERE matched
      AND node_id = (
          SELECT node_id
          FROM query_nodes
          WHERE parent_id IS NULL
      )
)
SELECT docname,
       score,
       rank
FROM ranked
CROSS JOIN query_params
WHERE query_params.result_limit IS NULL OR rank <= query_params.result_limit
UNION ALL
SELECT error(message)::VARCHAR AS docname,
       NULL::DOUBLE AS score,
       NULL::BIGINT AS rank
FROM selected_validation_error
ORDER BY rank;
