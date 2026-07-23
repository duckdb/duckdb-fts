INSERT INTO {{fts_schema}}.stopwords
SELECT * FROM {{stopwords_table}};
