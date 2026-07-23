CREATE TABLE {{fts_schema}}.raw_dict AS
WITH document_frequencies AS (
    SELECT rawtermid,
           count(DISTINCT docid)::BIGINT AS df
    FROM {{fts_schema}}.terms
    GROUP BY rawtermid
)
SELECT build_raw_dict.rawtermid,
       build_raw_dict.raw_term,
       build_raw_dict.termid,
       document_frequencies.df
FROM temp.{{build_raw_dict_table}} AS build_raw_dict
JOIN document_frequencies
  ON document_frequencies.rawtermid = build_raw_dict.rawtermid
ORDER BY build_raw_dict.rawtermid;
