CREATE TABLE {{fts_schema}}.{{terms_storage_table}} AS
SELECT termid, docid, fieldid FROM {{fts_schema}}.terms;
DROP TABLE {{fts_schema}}.terms;
CREATE VIEW {{fts_schema}}.terms AS
SELECT termid, docid, fieldid FROM {{fts_schema}}.{{terms_storage_table}};
