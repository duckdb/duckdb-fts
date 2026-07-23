normalized_field_terms AS (
    SELECT field_term_tf.docid,
           field_term_tf.termid,
           field_term_tf.rawtermid,
           field_term_tf.fieldid,
           field_term_tf.df,
           field_term_tf.expansion_weight,
           field_config.field_weight,
           field_term_tf.tf / (
               (1.0 - field_config.field_b)
               + field_config.field_b * (
                   list_extract(docs.field_lens, field_term_tf.fieldid + 1)
                   / field_config.avg_field_len
               )
           ) AS normalized_tf
    FROM field_term_tf
    JOIN {{fts_schema}}.docs AS docs
      ON docs.docid = field_term_tf.docid
    JOIN field_config
      ON field_config.fieldid = field_term_tf.fieldid
),
bm25f_term_frequencies AS (
    SELECT docid,
           termid,
           rawtermid,
           any_value(df) AS df,
           max(expansion_weight) AS expansion_weight,
           sum(field_weight * normalized_tf) AS pseudo_tf
    FROM normalized_field_terms
    GROUP BY docid,
             termid,
             rawtermid
),
bm25f_scores AS (
    SELECT bm25f_term_frequencies.docid,
           sum(
               expansion_weight
               * log(((((stats.num_docs - df) + 0.5) / (df + 0.5)) + 1))
               * (
                   (pseudo_tf * (k + 1))
                   / (pseudo_tf + k)
               )
           ) AS score
    FROM bm25f_term_frequencies
    CROSS JOIN {{fts_schema}}.stats AS stats
    GROUP BY bm25f_term_frequencies.docid
),
best_field_term_scores AS (
    SELECT docid,
           fieldid,
           field_weight,
           expansion_weight
           * log(((((stats.num_docs - df) + 0.5) / (df + 0.5)) + 1))
           * (
               (normalized_tf * (k + 1))
               / (normalized_tf + k)
           ) AS term_score
    FROM normalized_field_terms
    CROSS JOIN {{fts_schema}}.stats AS stats
),
per_field_scores AS (
    SELECT docid,
           fieldid,
           max(field_weight) * sum(term_score) AS field_score
    FROM best_field_term_scores
    GROUP BY docid,
             fieldid
),
best_field_scores AS (
    SELECT per_field_scores.docid,
           max(field_score)
               + params.tie_breaker * (sum(field_score) - max(field_score)) AS score
    FROM per_field_scores
    CROSS JOIN params
    GROUP BY per_field_scores.docid,
             params.tie_breaker
),
scores AS (
    SELECT bm25f_scores.*
    FROM bm25f_scores
    CROSS JOIN params
    WHERE params.scoring_model = 'bm25f'
    UNION ALL
    SELECT best_field_scores.*
    FROM best_field_scores
    CROSS JOIN params
    WHERE params.scoring_model = 'best_fields'
)
