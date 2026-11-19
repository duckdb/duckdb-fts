CREATE TRIGGER {{trigger_35_term_stats_df}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.term_stats AS ts
    SET df = d.df
    FROM {{fts_schema}}.dict AS d
    WHERE ts.termid = d.termid
      AND ts.termid IN (
          {{affected_termids}}
      );

CREATE TRIGGER {{trigger_36_term_stats_by_len_df}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.term_stats_by_len AS ts
    SET df = d.df
    FROM {{fts_schema}}.dict AS d
    WHERE ts.termid = d.termid
      AND ts.termid IN (
          {{affected_termids}}
      );

CREATE TRIGGER {{trigger_37_raw_dict_df}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.raw_dict AS rd
    SET df = rd.df + inserted_df.df
    FROM (
        SELECT t.rawtermid,
               count(DISTINCT t.docid)::BIGINT AS df
        FROM {{new_rows}} AS new_rows
        JOIN {{fts_schema}}.docs AS docs ON docs.name = new_rows.{{input_id}}
        JOIN {{fts_schema}}.terms AS t ON t.docid = docs.docid
        GROUP BY t.rawtermid
    ) AS inserted_df
    WHERE rd.rawtermid = inserted_df.rawtermid;
