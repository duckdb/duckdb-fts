CREATE MACRO {{fts_schema}}.tokenize_spans(s) AS fts_tokenize_spans(s, {{tokenizer}}, {{ignore}}, {{strip_accents}}, {{lower}});
