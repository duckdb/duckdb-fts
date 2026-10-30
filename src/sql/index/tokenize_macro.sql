-- Bind the stable autoload entry first while remaining usable in scalar contexts.
CREATE MACRO {{fts_schema}}.{{macro_name}}(s) AS
    CASE WHEN stem('', 'none') IS NULL THEN NULL
         ELSE {{tokenize_function}}(s::VARCHAR, {{tokenizer}}, {{ignore}}, {{strip_accents}}, {{lower}})
    END;
