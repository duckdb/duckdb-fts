SELECT unnest({{fts_schema}}.tokenize(fts_ii.{{input_value}})) AS w,
       fts_ii.rowid AS docid,
       {{field_id}} AS fieldid
FROM {{input_table}} AS fts_ii
