SELECT unnest({{fts_schema}}.tokenize(fts_di.{{input_value}})) AS w
FROM fts_old_rows AS fts_di
