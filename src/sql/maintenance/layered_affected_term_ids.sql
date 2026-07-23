SELECT DISTINCT d.termid
FROM stemmed_stopped AS ss
JOIN {{fts_schema}}.dict AS d ON ss.term = d.term
