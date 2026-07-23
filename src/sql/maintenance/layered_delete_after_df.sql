CREATE TRIGGER {{trigger_21_term_prefixes_prune}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    DELETE FROM {{fts_schema}}.term_prefixes
    WHERE rawtermid IN (
        SELECT rd.rawtermid
        FROM {{fts_schema}}.raw_dict AS rd
        WHERE rd.df = 0
    );

CREATE TRIGGER {{trigger_22_raw_dict_prune}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    DELETE FROM {{fts_schema}}.raw_dict
    WHERE df = 0;

CREATE TRIGGER {{trigger_23_term_stats_df}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.term_stats AS ts
    SET df = d.df
    FROM {{fts_schema}}.dict AS d
    WHERE ts.termid = d.termid
      AND ts.termid IN (
          {{affected_termids}}
      );

CREATE TRIGGER {{trigger_24_term_stats_by_len_df}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    UPDATE {{fts_schema}}.term_stats_by_len AS ts
    SET df = d.df
    FROM {{fts_schema}}.dict AS d
    WHERE ts.termid = d.termid
      AND ts.termid IN (
          {{affected_termids}}
      );

CREATE TRIGGER {{trigger_25_term_grams_prune}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    DELETE FROM {{fts_schema}}.term_grams
    WHERE termid IN (
        SELECT d.termid
        FROM {{fts_schema}}.dict AS d
        WHERE d.df = 0
          AND d.termid IN (
              {{affected_termids}}
          )
    );

CREATE TRIGGER {{trigger_26_term_stats_by_len_prune}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    DELETE FROM {{fts_schema}}.term_stats_by_len
    WHERE termid IN (
        SELECT d.termid
        FROM {{fts_schema}}.dict AS d
        WHERE d.df = 0
          AND d.termid IN (
              {{affected_termids}}
          )
    );

CREATE TRIGGER {{trigger_27_term_stats_prune}} AFTER DELETE ON {{input_table}}
REFERENCING OLD TABLE AS fts_old_rows
FOR EACH STATEMENT
    DELETE FROM {{fts_schema}}.term_stats
    WHERE termid IN (
        SELECT d.termid
        FROM {{fts_schema}}.dict AS d
        WHERE d.df = 0
          AND d.termid IN (
              {{affected_termids}}
          )
    );
