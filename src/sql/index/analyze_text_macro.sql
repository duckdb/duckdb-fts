CREATE MACRO {{fts_schema}}.analyze_text(s) AS TABLE
WITH fts_extension_autoload AS (
    -- Bind DuckDB's stable FTS autoload entry before the internal analyzer.
    SELECT stem('', 'none') AS marker
),
positioned_tokens AS (
    SELECT token.t.raw_term AS raw_term,
           token.t.start_offset AS start_offset,
           token.t.end_offset AS end_offset,
           token.position::UINTEGER AS position
    FROM fts_extension_autoload,
         UNNEST(
        list_filter(
            {{fts_schema}}.tokenize_spans(s),
            lambda value: value.raw_term IS NOT NULL AND value.raw_term <> ''
        )
    ) WITH ORDINALITY AS token(t, position)
),
tokenized AS (
    SELECT raw_term,
           position,
           start_offset,
           end_offset
    FROM positioned_tokens
),
analyzed_tokens AS (
    {{analyzed_tokens}}
),
token_stream AS (
    SELECT raw_term,
           term,
           position,
           start_offset,
           end_offset,
           (
               position
               - coalesce(lag(position) OVER (ORDER BY position), 0)
           )::UINTEGER AS position_increment
    FROM analyzed_tokens
)
SELECT raw_term,
       term,
       position,
       position_increment,
       1::UINTEGER AS position_length,
       start_offset,
       end_offset,
       'word'::VARCHAR AS token_type
FROM token_stream;
