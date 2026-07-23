fts_new_docs AS (
    SELECT COALESCE((SELECT max(docid) + 1 FROM {{fts_schema}}.docs), 0)
               + row_number() OVER (ORDER BY fts_new_rows.{{input_id}}) - 1 AS docid,
           fts_new_rows.{{input_id}} AS name,
           {{input_value_select_list}}
    FROM fts_new_rows
)
