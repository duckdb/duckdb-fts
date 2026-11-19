CREATE TRIGGER {{trigger_05_raw_dict_df}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.raw_dict AS rd
    SET df = rd.df - deleted_df.df
    FROM (
        SELECT t.rawtermid,
               count(DISTINCT t.docid)::BIGINT AS df
        FROM {{old_rows}} AS old_rows
        JOIN {{fts_schema}}.docs AS docs ON docs.name = old_rows.{{input_id}}
        JOIN {{fts_schema}}.terms AS t ON t.docid = docs.docid
        GROUP BY t.rawtermid
    ) AS deleted_df
    WHERE rd.rawtermid = deleted_df.rawtermid;
