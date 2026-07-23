SELECT unnest({{fts_schema}}.tokenize(fts_ii.{{input_value}})) AS w,
       fts_ii.docid AS docid,
       (SELECT fieldid FROM {{fts_schema}}.fields WHERE field = {{input_value_string}}) AS fieldid
FROM fts_new_docs AS fts_ii
