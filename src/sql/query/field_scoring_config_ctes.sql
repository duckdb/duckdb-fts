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
               list_filter(map_entries(params.field_weights), lambda e: lower(e.key) = lower(fts_fields.field))[1].value,
               1.0
           )::DOUBLE AS field_weight,
           coalesce(
               list_filter(map_entries(params.field_b), lambda e: lower(e.key) = lower(fts_fields.field))[1].value,
               params.default_b
           )::DOUBLE AS field_b,
           list_extract(
               stats.avg_field_lens,
               fts_fields.fieldid + 1
           ) AS avg_field_len
    FROM {{fts_schema}}.fields AS fts_fields
    CROSS JOIN params
    CROSS JOIN {{fts_schema}}.stats AS stats
    WHERE CASE WHEN fields IS NULL THEN true ELSE lower(fts_fields.field) IN (
        SELECT lower(requested_fields.field)
        FROM requested_fields
    ) END
),
