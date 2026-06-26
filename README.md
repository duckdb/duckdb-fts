# Full-Text Search Extension

Full-Text Search is an extension to DuckDB that allows for search through strings, similar to [SQLite's FTS5 extension](https://www.sqlite.org/fts5.html).

## Installing and Loading

The `fts` extension will be transparently autoloaded on first use from the official extension repository.
If you would like to install and load it manually, run:

```sql
INSTALL fts;
LOAD fts;
```

## Usage

The extension adds two `PRAGMA` statements to DuckDB: one to create, and one to drop an index. Additionally, a scalar macro `stem` is added, which is used internally by the extension.

### `PRAGMA create_fts_index`

```python
create_fts_index(input_table, input_id, *input_values, stemmer = 'porter',
                 stopwords = 'english', ignore = '(\\.|[^a-z])+',
                 strip_accents = 1, lower = 1, overwrite = 0,
                 incremental = 0, cluster_terms = 0, layered_search = 0)
```

`PRAGMA` that creates a FTS index for the specified table.

<!-- markdownlint-disable MD056 -->

| Name | Type | Description |
|:--|:--|:----------|
| `input_table` | `VARCHAR` | Qualified name of specified table, e.g., `'table_name'` or `'main.table_name'` |
| `input_id` | `VARCHAR` | Column name of document identifier, e.g., `'document_identifier'` |
| `input_values…` | `VARCHAR` | Column names of the text fields to be indexed (vararg), e.g., `'text_field_1'`, `'text_field_2'`, ..., `'text_field_N'`, or `'\*'` for all columns in input_table of type `VARCHAR` |
| `stemmer` | `VARCHAR` | The type of stemmer to be used. One of `'arabic'`, `'armenian'`, `'basque'`, `'catalan'`, `'czech'`, `'danish'`, `'dutch'`, `'dutch_porter'`, `'english'`, `'esperanto'`, `'estonian'`, `'finnish'`, `'french'`, `'german'`, `'greek'`, `'hindi'`, `'hungarian'`, `'indonesian'`, `'irish'`, `'italian'`, `'lithuanian'`, `'nepali'`, `'norwegian'`, `'persian'`, `'polish'`, `'porter'`, `'portuguese'`, `'romanian'`, `'russian'`, `'serbian'`, `'sesotho'`, `'spanish'`, `'swedish'`, `'tamil'`, `'turkish'`, `'yiddish'`, or `'none'` if no stemming is to be used. Defaults to `'porter'` |
| `stopwords` | `VARCHAR` | Qualified name of table containing a single `VARCHAR` column containing the desired stopwords, or `'none'` if no stopwords are to be used. Defaults to `'english'` for a pre-defined list of 571 English stopwords |
| `ignore` | `VARCHAR` | Regular expression of patterns to be ignored. Defaults to `'(\\.|[^a-z])+'`, ignoring all escaped and non-alphabetic lowercase characters |
| `strip_accents` | `BOOLEAN` | Whether to remove accents (e.g., convert `á` to `a`). Defaults to `1` |
| `lower` | `BOOLEAN` | Whether to convert all text to lowercase. Defaults to `1` |
| `overwrite` | `BOOLEAN` | Whether to overwrite an existing index on a table. Defaults to `0` |
| `incremental` | `BOOLEAN` | Whether to keep the index in sync for subsequent `INSERT` and `DELETE` statements using triggers. Defaults to `0` |
| `cluster_terms` | `BOOLEAN` | Whether to physically order the generated `terms` table by `termid`, `fieldid`, and `docid`. This can improve query-time pruning for direct reads from the FTS tables. Defaults to `0` |
| `layered_search` | `BOOLEAN` | Whether to build a dictionary trigram sidecar and layered BM25 search macros for exact, prefix, substring, and fuzzy query expansion. This implies `cluster_terms`. Defaults to `0` |

<!-- markdownlint-enable MD056 -->

This `PRAGMA` builds the index under a newly created schema. The schema will be named after the input table: if an index is created on table `'main.table_name'`, then the schema will be named `'fts_main_table_name'`.

### `PRAGMA drop_fts_index`

```python
drop_fts_index(input_table)
```

Drops a FTS index for the specified table.


| Name | Type | Description |
|:--|:--|:-----------|
| `input_table` | `VARCHAR` | Qualified name of input table, e.g., `'table_name'` or `'main.table_name'` |

### `match_bm25` Function

```python
match_bm25(input_id, query_string, fields := NULL, k := 1.2, b := 0.75, conjunctive := 0)
```

When an index is built, this retrieval macro is created that can be used to search the index.

| Name | Type | Description |
|:--|:--|:----------|
| `input_id` | `VARCHAR` | Column name of document identifier, e.g., `'document_identifier'` |
| `query_string` | `VARCHAR` | The string to search the index for |
| `fields` | `VARCHAR` | Comma-separarated list of fields to search in, e.g., `'text_field_2, text_field_N'`. Defaults to `NULL` to search all indexed fields |
| `k` | `DOUBLE` | Parameter _k<sub>1</sub>_ in the Okapi BM25 retrieval model. Defaults to `1.2` |
| `b` | `DOUBLE` | Parameter _b_ in the Okapi BM25 retrieval model. Defaults to `0.75` |
| `conjunctive` | `BOOLEAN` | Whether to make the query conjunctive i.e., all terms in the query string must be present in order for a document to be retrieved |

### Layered BM25 Search

```python
search_layered_bm25(query_string, fields := NULL, top_k := 50, k := 1.2,
                    b := 0.75, term_limit := 32, max_df_ratio := 0.15,
                    max_df := 50000, enable_prefix := true,
                    enable_substring := true, enable_fuzzy := true,
                    enable_short_fuzzy := true, expand_exact_terms := false)

match_layered_bm25(input_id, query_string, fields := NULL, k := 1.2,
                   b := 0.75, term_limit := 32, max_df_ratio := 0.15,
                   max_df := 50000, enable_prefix := true,
                   enable_substring := true, enable_fuzzy := true,
                   enable_short_fuzzy := true, expand_exact_terms := false)
```

When `layered_search` is enabled, the extension builds two additional
dictionary sidecar tables, `term_stats` and `term_grams`, plus a
length-clustered copy of the dictionary metadata. These tables grow with the
term dictionary rather than with the document corpus and are used to expand
query terms before scoring over the standard FTS `terms` table.

`search_layered_bm25` returns `docname`, `score`, and `rank`.
`match_layered_bm25` is the scalar form for use against an input table row.
Exact terms are always included. Prefix, substring, and fuzzy alternatives can
be toggled with the corresponding parameters. Numeric query terms are searched
exactly but are excluded from trigram/fuzzy expansion.

### `stem` Function

```python
stem(input_string, stemmer)
```

Reduces words to their base. Used internally by the extension.

| Name | Type | Description |
|:--|:--|:----------|
| `input_string` | `VARCHAR` | The column or constant to be stemmed. |
| `stemmer` | `VARCHAR` | The type of stemmer to be used. One of `'arabic'`, `'armenian'`, `'basque'`, `'catalan'`, `'czech'`, `'danish'`, `'dutch'`, `'dutch_porter'`, `'english'`, `'esperanto'`, `'estonian'`, `'finnish'`, `'french'`, `'german'`, `'greek'`, `'hindi'`, `'hungarian'`, `'indonesian'`, `'irish'`, `'italian'`, `'lithuanian'`, `'nepali'`, `'norwegian'`, `'persian'`, `'polish'`, `'porter'`, `'portuguese'`, `'romanian'`, `'russian'`, `'serbian'`, `'sesotho'`, `'spanish'`, `'swedish'`, `'tamil'`, `'turkish'`, `'yiddish'`, or `'none'` if no stemming is to be used. |

## Example Usage

Create a table and fill it with text data:

```sql
CREATE TABLE documents (
    document_identifier VARCHAR,
    text_content VARCHAR,
    author VARCHAR,
    doc_version INTEGER
);
INSERT INTO documents
    VALUES ('doc1',
            'The mallard is a dabbling duck that breeds throughout the temperate.',
            'Hannes Mühleisen',
            3),
           ('doc2',
            'The cat is a domestic species of small carnivorous mammal.',
            'Laurens Kuiper',
            2
           );
```

Build the index, and make both the `text_content` and `author` columns searchable.

```sql
PRAGMA create_fts_index(
    'documents', 'document_identifier', 'text_content', 'author'
);
```

Search the `author` field index for documents that are authored by `Muhleisen`. This retrieves `doc1`:

```sql
SELECT document_identifier, text_content, score
FROM (
    SELECT *, fts_main_documents.match_bm25(
        document_identifier,
        'Muhleisen',
        fields := 'author'
    ) AS score
    FROM documents
) sq
WHERE score IS NOT NULL
  AND doc_version > 2
ORDER BY score DESC;
```

| document_identifier |                             text_content                             | score |
|---------------------|----------------------------------------------------------------------|------:|
| doc1                | The mallard is a dabbling duck that breeds throughout the temperate. | 0.0   |

Search for documents about `small cats`. This retrieves `doc2`:

```sql
SELECT document_identifier, text_content, score
FROM (
    SELECT *, fts_main_documents.match_bm25(
        document_identifier,
        'small cats'
    ) AS score
    FROM documents
) sq
WHERE score IS NOT NULL
ORDER BY score DESC;
```

| document_identifier |                        text_content                        | score |
|---------------------|------------------------------------------------------------|------:|
| doc2                | The cat is a domestic species of small carnivorous mammal. | 0.0   |

> Warning Without `incremental = true`, the FTS index will not update
> automatically when the input table changes. With `incremental = true`, the
> index and layered sidecar are maintained for `INSERT` and `DELETE`
> statements on tables that support triggers.

## Stemmers

The extension bundles the [Snowball](https://snowballstem.org/) stemming
library (vendored under `third_party/snowball/`).

Starting with the upgrade to Snowball v3, the previously available
`'german2'` and `'kraaij_pohlmann'` stemmers (legacy variants that were
undocumented but accepted by the underlying library) have been removed
upstream. If you relied on them, rebuild the FTS index with one of the
documented stemmers instead.
