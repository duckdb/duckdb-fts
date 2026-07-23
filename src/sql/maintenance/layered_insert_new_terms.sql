CREATE TRIGGER {{trigger_15_term_stats}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
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

CREATE TRIGGER {{trigger_16_term_stats_by_len}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
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

CREATE TRIGGER {{trigger_17_term_grams}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
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

CREATE TRIGGER {{trigger_18_raw_dict}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
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

CREATE TRIGGER {{trigger_19_term_prefixes}} AFTER INSERT ON {{input_table}}
REFERENCING NEW TABLE AS fts_new_rows
FOR EACH STATEMENT
    INSERT INTO {{fts_schema}}.term_prefixes (prefix_len, prefix, rawtermid)
    {{token_ctes}},
    affected_raw_terms AS (
        SELECT DISTINCT rd.rawtermid,
               rd.raw_term
        FROM stemmed_stopped AS ss
        JOIN {{fts_schema}}.dict AS d ON d.term = ss.term
        JOIN {{fts_schema}}.raw_dict AS rd
          ON rd.raw_term = ss.raw_term
         AND rd.termid = d.termid
    )
    SELECT prefix_lengths.prefix_len,
           substr(affected_raw_terms.raw_term, 1, prefix_lengths.prefix_len) AS prefix,
           affected_raw_terms.rawtermid
    FROM affected_raw_terms,
         (VALUES (2::UTINYINT), (3::UTINYINT)) AS prefix_lengths(prefix_len)
    WHERE length(affected_raw_terms.raw_term) >= prefix_lengths.prefix_len
      AND NOT EXISTS (
          SELECT 1
          FROM {{fts_schema}}.term_prefixes AS tp
          WHERE tp.prefix_len = prefix_lengths.prefix_len
            AND tp.prefix = substr(affected_raw_terms.raw_term, 1, prefix_lengths.prefix_len)
            AND tp.rawtermid = affected_raw_terms.rawtermid
      )
    ORDER BY prefix_lengths.prefix_len,
             prefix,
             affected_raw_terms.rawtermid;
