CREATE TRIGGER {{trigger_15_term_stats}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.term_stats (termid, term, df, term_len, gram_count)
    {{token_ctes}},
    affected_terms AS (
        SELECT DISTINCT d.termid,
               d.term,
               d.df
        FROM stemmed_stopped AS ss
        JOIN {{fts_schema}}.dict AS d ON ss.term = d.term
        WHERE d.term <> ''
    )
    SELECT affected_terms.termid,
           affected_terms.term,
           affected_terms.df,
           length(affected_terms.term)::BIGINT AS term_len,
           greatest(length(affected_terms.term) - 2, 0)::BIGINT AS gram_count
    FROM affected_terms
    WHERE affected_terms.term <> ''
      AND NOT EXISTS (
          SELECT 1
          FROM {{fts_schema}}.term_stats AS ts
          WHERE ts.termid = affected_terms.termid
      )
    ORDER BY affected_terms.termid;

CREATE TRIGGER {{trigger_16_term_stats_by_len}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.term_stats_by_len (termid, term, df, term_len, gram_count)
    {{token_ctes}},
    affected_terms AS (
        SELECT DISTINCT d.termid
        FROM stemmed_stopped AS ss
        JOIN {{fts_schema}}.dict AS d ON ss.term = d.term
    )
    SELECT ts.termid,
           ts.term,
           ts.df,
           ts.term_len,
           ts.gram_count
    FROM {{fts_schema}}.term_stats AS ts
    JOIN affected_terms USING (termid)
    WHERE NOT EXISTS (
        SELECT 1
        FROM {{fts_schema}}.term_stats_by_len AS tsl
        WHERE tsl.termid = ts.termid
    )
    ORDER BY ts.term_len,
             ts.df,
             ts.termid;

CREATE TRIGGER {{trigger_17_term_grams}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.term_grams (gram, termid)
    {{token_ctes}},
    affected_terms AS (
        SELECT DISTINCT d.termid
        FROM stemmed_stopped AS ss
        JOIN {{fts_schema}}.dict AS d ON ss.term = d.term
    ),
    grams AS (
        SELECT 'g' || lower(hex(substr(ts.term, i, 3))) AS gram,
               ts.termid
        FROM {{fts_schema}}.term_stats AS ts,
             affected_terms AS affected_terms,
             range(1, ts.gram_count + 1) AS r(i)
        WHERE ts.termid = affected_terms.termid
          AND ts.gram_count > 0
          AND NOT regexp_full_match(ts.term, '[0-9]+')
    )
    SELECT grams.gram,
           grams.termid
    FROM grams
    WHERE NOT EXISTS (
        SELECT 1
        FROM {{fts_schema}}.term_grams AS tg
        WHERE tg.termid = grams.termid
          AND tg.gram = grams.gram
    )
    ORDER BY grams.gram,
             grams.termid;

CREATE TRIGGER {{trigger_18_raw_dict}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.raw_dict (rawtermid, raw_term, termid, df)
    {{token_ctes}},
    new_raw_terms AS (
        SELECT DISTINCT ss.raw_term,
               d.termid
        FROM stemmed_stopped AS ss
        JOIN {{fts_schema}}.dict AS d ON d.term = ss.term
        WHERE NOT EXISTS (
            SELECT 1
            FROM {{fts_schema}}.raw_dict AS rd
            WHERE rd.raw_term = ss.raw_term
              AND rd.termid = d.termid
        )
        ORDER BY ss.raw_term,
                 d.termid
    )
    SELECT (SELECT COALESCE(max(rawtermid) + 1, 0) FROM {{fts_schema}}.raw_dict)
               + row_number() OVER () - 1 AS rawtermid,
           new_raw_terms.raw_term,
           new_raw_terms.termid,
           0 AS df
    FROM new_raw_terms;

-- Trigger 18 inserts new raw terms with df = 0. The sidecar triggers consume
-- those rows before trigger 37 updates their document frequencies.
CREATE TRIGGER {{trigger_19_term_prefixes}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.term_prefixes (prefix_len, prefix, rawtermid)
    WITH
    affected_raw_terms AS (
        SELECT rd.rawtermid,
               rd.raw_term
        FROM {{fts_schema}}.raw_dict AS rd
        WHERE rd.df = 0
    )
    SELECT prefix_lengths.prefix_len,
           substr(affected_raw_terms.raw_term, 1, prefix_lengths.prefix_len) AS prefix,
           affected_raw_terms.rawtermid
    FROM affected_raw_terms,
         (VALUES (2::UTINYINT), (3::UTINYINT)) AS prefix_lengths(prefix_len)
    WHERE length(affected_raw_terms.raw_term) >= prefix_lengths.prefix_len
    ORDER BY prefix_lengths.prefix_len,
             prefix,
             affected_raw_terms.rawtermid;

CREATE TRIGGER {{trigger_19_raw_term_grams}} AFTER {{trigger_event}} ON {{input_table}}
REFERENCING {{transition_tables}}
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.raw_term_grams (gram, rawtermid)
    WITH
    affected_raw_terms AS (
        SELECT rd.rawtermid,
               rd.raw_term,
               d.term
        FROM {{fts_schema}}.raw_dict AS rd
        JOIN {{fts_schema}}.dict AS d USING (termid)
        WHERE rd.df = 0
    ),
    grams AS (
        SELECT unnest(list_distinct([
                   'g' || lower(hex(substr(affected_raw_terms.raw_term, i, 3)))
                   FOR i IN range(1, length(affected_raw_terms.raw_term) - 1)
               ])) AS gram,
               affected_raw_terms.rawtermid
        FROM affected_raw_terms
        WHERE length(affected_raw_terms.raw_term) >= 3
          AND (
              affected_raw_terms.raw_term <> affected_raw_terms.term
              OR regexp_full_match(affected_raw_terms.raw_term, '[0-9]+')
          )
    )
    SELECT grams.gram,
           grams.rawtermid
    FROM grams
    ORDER BY grams.gram,
             grams.rawtermid;
