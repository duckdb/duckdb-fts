CREATE MACRO {{fts_schema}}.__highlight_spans(s, query_string, query_mode) AS TABLE
WITH fts_extension_autoload AS (
    -- Bind DuckDB's stable FTS autoload entry before the internal analyzer.
    SELECT stem('', 'none') AS marker
),
text_tokens AS (
    SELECT raw_term,
           term,
           position,
           start_offset,
           end_offset
    FROM fts_extension_autoload,
         {{fts_schema}}.analyze_text(s)
),
query_tokens AS (
    SELECT raw_term,
           term,
           position AS query_position,
           row_number() OVER (ORDER BY position) AS query_index,
           count(*) OVER () AS query_count
    FROM (
        SELECT raw_term, term, position
        FROM {{fts_schema}}.analyze_text(query_string)
        {{trailing_stopword_token}}
    ) AS query_stream
),
query_bounds AS (
    SELECT min(query_position) AS first_position,
           max(query_position) AS last_position
    FROM query_tokens
)
-- Joins and aggregates only: a scalar or IN subquery over these row-dependent CTEs returned
-- the spans of other rows when several rows ran in one query on the pinned DuckDB.
SELECT text_tokens.start_offset AS span_start,
       text_tokens.end_offset AS span_end
FROM text_tokens
JOIN (SELECT DISTINCT term FROM query_tokens) AS query_terms
  ON query_terms.term = text_tokens.term
WHERE lower(query_mode::VARCHAR) IN ('standard', 'near')
UNION ALL
SELECT text_tokens.start_offset,
       text_tokens.end_offset
FROM text_tokens
JOIN query_tokens
  ON (query_tokens.query_index < query_tokens.query_count AND text_tokens.term = query_tokens.term)
  OR (query_tokens.query_index = query_tokens.query_count AND starts_with(text_tokens.raw_term, query_tokens.raw_term))
-- Search needs at least two characters in the unfinished word and finds nothing otherwise.
JOIN query_tokens AS final_token
  ON final_token.query_index = final_token.query_count
 AND length(final_token.raw_term) >= 2
WHERE lower(query_mode::VARCHAR) = 'autocomplete'
UNION ALL
SELECT anchors.start_offset,
       max(members.end_offset)
FROM text_tokens AS anchors
CROSS JOIN query_bounds
JOIN query_tokens
  ON true
JOIN text_tokens AS members
  ON members.position = anchors.position + (query_tokens.query_position - query_bounds.first_position)
 -- phrase_prefix: the last slot is the unfinished word, matched on the raw term only; every other slot on the analyzed term.
 AND CASE
         WHEN lower(query_mode::VARCHAR) = 'phrase_prefix'
          AND query_tokens.query_position = query_bounds.last_position
             THEN length(query_tokens.raw_term) >= 2
              AND starts_with(members.raw_term, query_tokens.raw_term)
         ELSE members.term = query_tokens.term
     END
WHERE lower(query_mode::VARCHAR) IN ('phrase', 'phrase_prefix')
GROUP BY anchors.position, anchors.start_offset, query_tokens.query_count
HAVING count(*) = query_tokens.query_count;

CREATE MACRO {{fts_schema}}.highlight(s, query_string, before, after, query_mode := 'standard') AS (
    WITH matched_spans AS (
        SELECT spans.span_start,
               spans.span_end
        FROM {{fts_schema}}.__highlight_spans(s, query_string, query_mode) AS spans
    )
    SELECT CASE
        WHEN s IS NULL THEN NULL
        WHEN lower(coalesce(query_mode::VARCHAR, '')) NOT IN ('standard', 'autocomplete', 'phrase', 'phrase_prefix', 'near')
            THEN error('query_mode must be one of standard, autocomplete, phrase, phrase_prefix, or near')
        ELSE fts_render_highlight(
            s,
            coalesce((SELECT list(matched_spans.span_start ORDER BY matched_spans.span_start, matched_spans.span_end) FROM matched_spans), []),
            coalesce((SELECT list(matched_spans.span_end ORDER BY matched_spans.span_start, matched_spans.span_end) FROM matched_spans), []),
            before,
            after,
            0,
            strlen(s)::UINTEGER,
            false,
            false,
            ''
        )
    END
);

CREATE MACRO {{fts_schema}}.snippet(s, query_string, before, after, ellipsis, max_tokens, query_mode := 'standard') AS (
    WITH matched_spans AS (
        SELECT spans.span_start,
               spans.span_end
        FROM {{fts_schema}}.__highlight_spans(s, query_string, query_mode) AS spans
    ),
    text_tokens AS (
        SELECT start_offset,
               end_offset,
               row_number() OVER (ORDER BY position) AS token_index
        FROM {{fts_schema}}.analyze_text(s)
    ),
    -- Clamped in HUGEINT, the widest type the check below accepts, before narrowing, so a budget above
    -- BIGINT cannot turn into NULL and shrink the window.
    window_size AS (
        SELECT least(greatest(try_cast(max_tokens AS HUGEINT), 1),
                     greatest((SELECT count(*) FROM text_tokens), 1))::BIGINT AS window_tokens
    ),
    -- The window centers the first matched token and clamps to the document.
    window_start AS (
        SELECT greatest(1, least(
                   coalesce(
                       (SELECT min(text_tokens.token_index)
                        FROM text_tokens
                        JOIN matched_spans ON text_tokens.start_offset = matched_spans.span_start),
                       1
                   ) - (window_size.window_tokens - 1) // 2,
                   (SELECT count(*) FROM text_tokens) - window_size.window_tokens + 1
               )) AS window_index
        FROM window_size
    ),
    -- An edge that cuts no token extends to the text boundary, so leading
    -- whitespace and trailing punctuation survive an untruncated side.
    window_bounds AS (
        SELECT CASE
                   WHEN window_start.window_index > 1
                       THEN (SELECT min(text_tokens.start_offset) FROM text_tokens WHERE text_tokens.token_index >= window_start.window_index)
                   ELSE 0
               END AS window_from,
               CASE
                   WHEN (SELECT count(*) FROM text_tokens) >= window_start.window_index + window_size.window_tokens
                       THEN (SELECT max(text_tokens.end_offset) FROM text_tokens WHERE text_tokens.token_index < window_start.window_index + window_size.window_tokens)
                   ELSE strlen(s)::UINTEGER
               END AS window_to,
               window_start.window_index > 1 AS leading_cut,
               (SELECT count(*) FROM text_tokens) >= window_start.window_index + window_size.window_tokens AS trailing_cut
        FROM window_start, window_size
    )
    SELECT CASE
        WHEN s IS NULL THEN NULL
        WHEN lower(coalesce(query_mode::VARCHAR, '')) NOT IN ('standard', 'autocomplete', 'phrase', 'phrase_prefix', 'near')
            THEN error('query_mode must be one of standard, autocomplete, phrase, phrase_prefix, or near')
        WHEN try_cast(max_tokens AS HUGEINT) IS NULL
             OR try_cast(max_tokens AS HUGEINT) < 1
             OR try_cast(max_tokens AS HUGEINT) <> max_tokens
             OR try_cast(max_tokens AS DOUBLE) <> floor(try_cast(max_tokens AS DOUBLE))
            THEN error('max_tokens must be a positive integer')
        WHEN (SELECT count(*) FROM text_tokens) = 0 THEN s
        ELSE fts_render_highlight(
            s,
            coalesce((SELECT list(matched_spans.span_start ORDER BY matched_spans.span_start, matched_spans.span_end) FROM matched_spans), []),
            coalesce((SELECT list(matched_spans.span_end ORDER BY matched_spans.span_start, matched_spans.span_end) FROM matched_spans), []),
            before,
            after,
            (SELECT window_bounds.window_from FROM window_bounds),
            (SELECT window_bounds.window_to FROM window_bounds),
            (SELECT window_bounds.leading_cut FROM window_bounds),
            (SELECT window_bounds.trailing_cut FROM window_bounds),
            ellipsis
        )
    END
);
