# This file is included by DuckDB's build system. It specifies which extension to load
set(FTS_DONT_LINK DONT_LINK)
if (LINK_FTS_STATICALLY)
    set(FTS_DONT_LINK "")
endif()

duckdb_extension_load(icu)
duckdb_extension_load(json)

# Extension from this repo
duckdb_extension_load(fts
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/src/include
    ${LOAD_FTS_TESTS}
    ${FTS_DONT_LINK}
)
