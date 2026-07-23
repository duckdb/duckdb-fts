UPDATE {{fts_schema}}.stats AS fts_stats
SET num_docs = fts_stats.num_docs {{operation}} delta.num_docs,
    avgdl = CASE
        WHEN fts_stats.num_docs {{operation}} delta.num_docs = 0 THEN NULL
        ELSE (
            fts_stats.total_len {{operation}} delta.total_len
        )::DOUBLE / (
            fts_stats.num_docs {{operation}} delta.num_docs
        )
    END,
    avg_field_lens = CASE
        WHEN fts_stats.num_docs {{operation}} delta.num_docs = 0
            THEN NULL::DOUBLE[]
        ELSE {{average_field_lengths}}
    END,
    total_len = fts_stats.total_len {{operation}} delta.total_len,
    total_field_lens = {{total_field_lengths}}
FROM (
    SELECT count(*)::BIGINT AS num_docs,
           coalesce(sum(docs.len), 0)::HUGEINT AS total_len,
           {{delta_field_lengths}}
    FROM {{fts_schema}}.docs AS docs
    JOIN {{transition_table}} AS changed_rows
      ON docs.name = changed_rows.{{input_id}}
) AS delta
