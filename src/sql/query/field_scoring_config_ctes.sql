requested_fields AS (
    SELECT trim(field_name) AS field
    FROM (
        SELECT unnest(string_split(fields, ',')) AS field_name
    ) AS split_fields
    WHERE trim(field_name) <> ''
),
{{field_scoring_validation_ctes}}
field_config AS (
    SELECT fts_fields.fieldid,
           coalesce(
               map_extract_value(params.field_weights, fts_fields.field),
               1.0
           )::DOUBLE AS field_weight,
           coalesce(
               map_extract_value(params.field_b, fts_fields.field),
               params.default_b
           )::DOUBLE AS field_b,
           list_extract(
               stats.avg_field_lens,
               fts_fields.fieldid + 1
           ) AS avg_field_len
    FROM {{fts_schema}}.fields AS fts_fields
    CROSS JOIN params
    CROSS JOIN {{fts_schema}}.stats AS stats
    WHERE CASE WHEN fields IS NULL THEN true ELSE fts_fields.field IN (
        SELECT requested_fields.field
        FROM requested_fields
    ) END
),
