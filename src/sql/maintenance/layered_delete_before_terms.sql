CREATE TRIGGER {{trigger_05_raw_dict_df}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.raw_dict AS rd
    SET df = rd.df - deleted_df.df
    FROM (
        SELECT t.rawtermid,
               count(DISTINCT t.docid)::BIGINT AS df
        FROM {{fts_schema}}.terms AS t
        JOIN {{fts_schema}}.docs AS docs ON docs.docid = t.docid
        JOIN {{old_rows}} AS old_rows ON old_rows.{{input_id}} = docs.name
        GROUP BY t.rawtermid
    ) AS deleted_df
    WHERE rd.rawtermid = deleted_df.rawtermid;
