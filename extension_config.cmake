# This file is included by DuckDB's build system. It specifies which extension to load

duckdb_extension_load(json)

# Extension from this repo
duckdb_extension_load(fts
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/src/include
    ${LOAD_FTS_TESTS}
)
duckdb_extension_statically_link(fts)
