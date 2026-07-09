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
                 tokenizer = 'regex',
                 stopwords = 'english',
                 ignore = "[0-9!@#$%^&*()_+={}\\[\\]:;<>,.?~\\\\/\\|''\"`-]+",
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
| `tokenizer` | `VARCHAR` | Tokenizer to use. `'regex'` keeps the legacy regex-split behavior. `'opensearch_standard'` uses an OpenSearch/Lucene standard-tokenizer compatibility mode that splits Han, Hiragana, and Katakana into single-character tokens while preserving word runs for scripts such as Hebrew, Cyrillic, Arabic, Latin, and Hangul. Defaults to `'regex'` |
| `stopwords` | `VARCHAR` | Qualified name of table containing a single `VARCHAR` column containing the desired stopwords, or `'none'` if no stopwords are to be used. Defaults to `'english'` for a pre-defined list of 571 English stopwords |
| `ignore` | `VARCHAR` | Regular expression of patterns to be ignored by the `'regex'` tokenizer. Defaults to a punctuation and digit pattern |
| `strip_accents` | `BOOLEAN` | Whether to remove accents (e.g., convert `á` to `a`). Defaults to `1` |
| `lower` | `BOOLEAN` | Whether to convert all text to lowercase. Defaults to `1` |
| `overwrite` | `BOOLEAN` | Whether to overwrite an existing index on a table. Defaults to `0` |
| `incremental` | `BOOLEAN` | Whether to keep the index in sync for subsequent `INSERT` and `DELETE` statements using triggers. Defaults to `0` |
| `cluster_terms` | `BOOLEAN` | Whether to physically order the generated `terms` table by `termid`, `fieldid`, and `docid`. This can improve query-time pruning for direct reads from the FTS tables. Defaults to `0` |
| `layered_search` | `BOOLEAN` | Whether to build a dictionary trigram sidecar and layered BM25 search macros for exact, prefix, substring, and fuzzy query expansion. This implies `cluster_terms`. Defaults to `0` |

<!-- markdownlint-enable MD056 -->

This `PRAGMA` builds the index under a newly created schema. The schema will be named after the input table: if an index is created on table `'main.table_name'`, then the schema will be named `'fts_main_table_name'`.

By default, indexes are static snapshots. If the input table changes after
index creation, rebuild the index with `overwrite = true` or use
`incremental = true` when creating the index. Incremental indexes are maintained
with triggers for `INSERT` and `DELETE` statements. They require trigger
support, a document id column declared `NOT NULL` or `PRIMARY KEY`, and unique
document id values. Persistent databases must use storage version `v2.0.0` or
newer for incremental indexes.

`cluster_terms = true` changes only the physical ordering of the generated
`terms` table. It cannot be combined with `incremental = true` unless
`layered_search = true`, because incremental inserts do not preserve the static
clustered layout.

### `PRAGMA drop_fts_index`

```python
drop_fts_index(input_table)
```

Drops a FTS index for the specified table. This removes the generated FTS
schema and any triggers used for incremental maintenance. Recreating an index
with `overwrite = true` performs the same cleanup before building the new index.


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
| `fields` | `VARCHAR` | Comma-separated list of fields to search in, e.g., `'text_field_2, text_field_N'`. Defaults to `NULL` to search all indexed fields |
| `k` | `DOUBLE` | Parameter _k<sub>1</sub>_ in the Okapi BM25 retrieval model. Defaults to `1.2` |
| `b` | `DOUBLE` | Parameter _b_ in the Okapi BM25 retrieval model. Defaults to `0.75` |
| `conjunctive` | `BOOLEAN` | Whether to make the query conjunctive, i.e., all query terms that remain after tokenization, stopword removal, and stemming must be present for a document to be retrieved |

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

When `layered_search` is enabled, the extension builds dictionary sidecar
tables used to expand query terms before scoring over the standard FTS `terms`
table. These sidecar tables grow with the term dictionary rather than with the
document corpus.

`search_layered_bm25` is a table macro returning `docname`, `score`, and
`rank`. `match_layered_bm25` is the scalar form for use against an input table
row. Both macros use the same tokenization, stopword removal, stemming, field
filtering, and BM25 parameters as the base FTS index.

<!-- markdownlint-disable MD056 -->

| Name | Type | Description |
|:--|:--|:----------|
| `input_id` | `VARCHAR` | Document identifier to score. Only used by `match_layered_bm25` |
| `query_string` | `VARCHAR` | The string to search the index for |
| `fields` | `VARCHAR` | Comma-separated list of indexed fields to search. Defaults to `NULL` to search all indexed fields |
| `top_k` | `BIGINT` | Maximum number of rows returned by `search_layered_bm25`. Defaults to `50`; use `NULL` to return all matches |
| `k` | `DOUBLE` | Parameter _k<sub>1</sub>_ in the Okapi BM25 retrieval model. Defaults to `1.2` |
| `b` | `DOUBLE` | Parameter _b_ in the Okapi BM25 retrieval model. Defaults to `0.75` |
| `term_limit` | `BIGINT` | Maximum number of expanded alternatives to include per query term. Defaults to `32` |
| `max_df_ratio` | `DOUBLE` | Maximum document-frequency ratio for terms considered during expansion. Defaults to `0.15` |
| `max_df` | `BIGINT` | Absolute document-frequency cap for terms considered during expansion. Defaults to `50000` |
| `enable_prefix` | `BOOLEAN` | Whether to include dictionary terms that start with a query term. Defaults to `true` |
| `enable_substring` | `BOOLEAN` | Whether to include dictionary terms that contain a query term. Defaults to `true` |
| `enable_fuzzy` | `BOOLEAN` | Whether to include Damerau-Levenshtein fuzzy alternatives. Defaults to `true` |
| `enable_short_fuzzy` | `BOOLEAN` | Whether to use a length-clustered path for short fuzzy alternatives. Defaults to `true` |
| `expand_exact_terms` | `BOOLEAN` | Whether to also expand a query term that already has an exact dictionary match. Defaults to `false` |

<!-- markdownlint-enable MD056 -->

Exact terms are always included in the candidate set. Prefix, substring, and
fuzzy alternatives are optional and receive lower expansion weights before BM25
scoring. Numeric query terms are searched exactly and are excluded from
trigram/fuzzy expansion. Stopwords are removed before both exact matching and
expansion, so a query containing only stopwords returns no rows.

Layered search can be static or incremental. With `layered_search = true` and
`incremental = false`, the sidecar tables are built once and later table
changes are not visible until the index is rebuilt. With both options enabled,
the sidecar tables are maintained together with the base FTS index for
`INSERT` and `DELETE`.

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

Build an incremental index when inserts and deletes should update the index
automatically. The document identifier must be non-null and unique:

```sql
CREATE TABLE live_documents (
    document_identifier VARCHAR NOT NULL,
    text_content VARCHAR
);

INSERT INTO live_documents
    VALUES ('doc1', 'quacking quacking'),
           ('doc2', 'barking barking');

PRAGMA create_fts_index(
    'live_documents',
    'document_identifier',
    'text_content',
    incremental = true
);

INSERT INTO live_documents VALUES ('doc3', 'meowing');

SELECT document_identifier
FROM (
    SELECT *, fts_main_live_documents.match_bm25(
        document_identifier,
        'meowing'
    ) AS score
    FROM live_documents
) sq
WHERE score IS NOT NULL;
```

Use layered search when query terms should match exact terms plus prefix,
substring, or typo-tolerant alternatives:

```sql
CREATE TABLE animal_sounds (
    document_identifier VARCHAR NOT NULL,
    text_content VARCHAR,
    author VARCHAR
);

INSERT INTO animal_sounds
    VALUES ('doc1', 'quacking quacking', 'Hannes'),
           ('doc2', 'barking barking', 'Mark'),
           ('doc3', 'meowing meowing', 'Laurens');

PRAGMA create_fts_index(
    'animal_sounds',
    'document_identifier',
    'text_content',
    'author',
    stemmer = 'none',
    stopwords = 'none',
    layered_search = true,
    incremental = true
);

SELECT docname, score, rank
FROM fts_main_animal_sounds.search_layered_bm25(
    'quack',
    fields := 'text_content',
    top_k := 10
);
```

The scalar layered helper can be used in the same row-filtering pattern as
`match_bm25`:

```sql
SELECT document_identifier, text_content, score
FROM (
    SELECT *, fts_main_animal_sounds.match_layered_bm25(
        document_identifier,
        'mark',
        fields := 'author',
        enable_short_fuzzy := false
    ) AS score
    FROM animal_sounds
) sq
WHERE score IS NOT NULL
ORDER BY score DESC;
```

By default, exact dictionary matches are not further expanded. Set
`expand_exact_terms := true` to include alternatives for exact query terms:

```sql
SELECT docname
FROM fts_main_animal_sounds.search_layered_bm25(
    'mark',
    fields := 'author',
    expand_exact_terms := true
);
```

> Warning Without `incremental = true`, the FTS index is a static snapshot and
> will not update automatically when the input table changes. Static layered
> indexes behave the same way: their sidecar tables are also rebuilt only when
> the index is rebuilt. With `incremental = true`, the index and layered
> sidecar are maintained for `INSERT` and `DELETE` statements on tables that
> support triggers.

## Stemmers

The extension bundles the [Snowball](https://snowballstem.org/) stemming
library (vendored under `third_party/snowball/`).

Starting with the upgrade to Snowball v3, the previously available
`'german2'` and `'kraaij_pohlmann'` stemmers (legacy variants that were
undocumented but accepted by the underlying library) have been removed
upstream. If you relied on them, rebuild the FTS index with one of the
documented stemmers instead.
