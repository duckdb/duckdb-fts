CREATE TRIGGER {{trigger_00_dict_df}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.dict AS d
    SET df = d.df - deleted_df_delta.df_delta
    FROM (
        SELECT t.termid,
               COUNT(DISTINCT t.docid) AS df_delta
        FROM fts_old_rows AS old_rows
        JOIN {{fts_schema}}.docs AS docs ON docs.name = old_rows.{{input_id}}
        JOIN {{fts_schema}}.terms AS t ON t.docid = docs.docid
        GROUP BY t.termid
    ) AS deleted_df_delta
    WHERE d.termid = deleted_df_delta.termid;

{{layered_delete_before_terms_triggers}}

CREATE TRIGGER {{trigger_10_terms}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    DELETE FROM {{fts_schema}}.terms
    WHERE docid IN (
        SELECT d.docid
        FROM {{fts_schema}}.docs AS d
        JOIN fts_old_rows AS old_rows ON d.name = old_rows.{{input_id}}
    );

CREATE TRIGGER {{trigger_15_stats}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    {{stats_delta_update}};

CREATE TRIGGER {{trigger_20_docs}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    DELETE FROM {{fts_schema}}.docs
    WHERE name IN (SELECT {{input_id}} FROM fts_old_rows);

{{layered_delete_after_df_triggers}}

CREATE TRIGGER {{trigger_30_dict_prune}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    DELETE FROM {{fts_schema}}.dict
    WHERE df = 0;
