cmake_minimum_required(VERSION 2.8.12...3.29)
set(TARGET_NAME fts)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(FTSExtension)

set(EXTENSION_BASE_FOLDER "src")
set(FTS_SQL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FTS_SQL_ASSETS
    "SCHEMA_SETUP|src/sql/index/schema_setup.sql"
    "ENGLISH_STOPWORDS|src/sql/index/english_stopwords.sql"
    "IMPORT_STOPWORDS|src/sql/index/import_stopwords.sql"
    "TOKENIZE_MACRO|src/sql/index/tokenize_macro.sql"
    "INDEX_TABLES|src/sql/index/index_tables.sql"
    "TOKENIZE_FIELD|src/sql/index/tokenize_field.sql"
    "BUILD_RAW_DICT|src/sql/index/build_raw_dict.sql"
    "RAW_DICT_TABLE|src/sql/index/raw_dict_table.sql"
    "LAYERED_SIDECARS|src/sql/index/layered_sidecars.sql"
    "FIELD_SCORING_VALIDATION_CTES|src/sql/query/field_scoring_validation_ctes.sql"
    "FIELD_SCORING_CONFIG_CTES|src/sql/query/field_scoring_config_ctes.sql"
    "FIELD_SCORING_SCORE_CTES|src/sql/query/field_scoring_score_ctes.sql"
    "MATCH_BM25|src/sql/query/match_bm25.sql"
    "SEARCH_LAYERED_BM25|src/sql/query/search_layered_bm25.sql"
    "MATCH_LAYERED_BM25|src/sql/query/match_layered_bm25.sql"
    "SEARCH_LAYERED_BM25_QUERY|src/sql/query/search_layered_bm25_query.sql"
    "MATCH_LAYERED_BM25_QUERY|src/sql/query/match_layered_bm25_query.sql"
    "INCREMENTAL_INDEX_SETUP|src/sql/maintenance/incremental_index_setup.sql"
    "CLUSTERED_INCREMENTAL_INDEX_SETUP|src/sql/maintenance/clustered_incremental_index_setup.sql"
    "STATS_DELTA_UPDATE|src/sql/maintenance/stats_delta_update.sql"
    "LAYERED_AFFECTED_TERM_IDS|src/sql/maintenance/layered_affected_term_ids.sql"
    "LAYERED_INSERT_NEW_TERMS|src/sql/maintenance/layered_insert_new_terms.sql"
    "LAYERED_INSERT_DF|src/sql/maintenance/layered_insert_df.sql"
    "LAYERED_DELETE_BEFORE_TERMS|src/sql/maintenance/layered_delete_before_terms.sql"
    "LAYERED_DELETE_AFTER_DF|src/sql/maintenance/layered_delete_after_df.sql"
    "INSERT_DOCS_CTE|src/sql/maintenance/insert_docs_cte.sql"
    "INSERT_TOKEN_DOCS_CTE|src/sql/maintenance/insert_token_docs_cte.sql"
    "TOKENIZE_INSERTED_FIELD|src/sql/maintenance/tokenize_inserted_field.sql"
    "LAYERED_INSERT_TOKEN_CTES|src/sql/maintenance/layered_insert_token_ctes.sql"
    "INSERT_TRIGGERS|src/sql/maintenance/insert_triggers.sql"
    "CLUSTERED_INSERT_TOKEN_CTES|src/sql/maintenance/clustered_insert_token_ctes.sql"
    "CLUSTERED_INSERT_TRIGGERS|src/sql/maintenance/clustered_insert_triggers.sql"
    "TOKENIZE_DELETED_FIELD|src/sql/maintenance/tokenize_deleted_field.sql"
    "DELETE_TOKEN_CTES|src/sql/maintenance/delete_token_ctes.sql"
    "DELETE_TRIGGERS|src/sql/maintenance/delete_triggers.sql"
    "CLUSTERED_DELETE_TRIGGERS|src/sql/maintenance/clustered_delete_triggers.sql")
include(cmake/GenerateFTSSQLAssets.cmake)
generate_fts_sql_assets("${FTS_SQL_GENERATED_DIR}" ${FTS_SQL_ASSETS})

set(SNOWBALL_BASE_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/third_party/snowball")
if(DEFINED DUCKDB_MODULE_BASE_DIR)
  set(DUCKDB_SOURCE_BASE_FOLDER "${DUCKDB_MODULE_BASE_DIR}")
else()
  set(DUCKDB_SOURCE_BASE_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/duckdb")
endif()
set(DUCKDB_ICU_BASE_FOLDER "${DUCKDB_SOURCE_BASE_FOLDER}/extension/icu")
include_directories(
    ${EXTENSION_BASE_FOLDER}/include
    ${FTS_SQL_GENERATED_DIR}
    ${SNOWBALL_BASE_FOLDER}/include
    ${SNOWBALL_BASE_FOLDER}/libstemmer
    ${SNOWBALL_BASE_FOLDER}/runtime
    ${DUCKDB_SOURCE_BASE_FOLDER}/third_party/utf8proc/include
    ${DUCKDB_ICU_BASE_FOLDER}/third_party/icu/common
    ${DUCKDB_ICU_BASE_FOLDER}/third_party/icu/i18n)

file(GLOB SNOWBALL_STEMMER_SOURCES CONFIGURE_DEPENDS ${SNOWBALL_BASE_FOLDER}/src_c/stem_*.c)

set(EXTENSION_SOURCES
    ${EXTENSION_BASE_FOLDER}/fts_extension.cpp
    ${EXTENSION_BASE_FOLDER}/fts_index_builder.cpp
    ${EXTENSION_BASE_FOLDER}/fts_index_common.cpp
    ${EXTENSION_BASE_FOLDER}/fts_index_maintenance.cpp
    ${EXTENSION_BASE_FOLDER}/fts_indexing.cpp
    ${EXTENSION_BASE_FOLDER}/fts_sql_template.cpp
    ${FTS_SQL_ASSETS_SOURCE}
    ${SNOWBALL_BASE_FOLDER}/libstemmer/libstemmer.c
    ${SNOWBALL_BASE_FOLDER}/runtime/api.c
    ${SNOWBALL_BASE_FOLDER}/runtime/utilities.c
    ${SNOWBALL_STEMMER_SOURCES})

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

if(TARGET duckdb_icu_common)
  target_sources(${EXTENSION_NAME} PRIVATE $<TARGET_OBJECTS:duckdb_icu_common>)
  target_sources(${LOADABLE_EXTENSION_NAME} PRIVATE $<TARGET_OBJECTS:duckdb_icu_common>)
endif()
if(TARGET duckdb_icu_common_unity)
  target_sources(${EXTENSION_NAME} PRIVATE $<TARGET_OBJECTS:duckdb_icu_common_unity>)
  target_sources(${LOADABLE_EXTENSION_NAME} PRIVATE $<TARGET_OBJECTS:duckdb_icu_common_unity>)
endif()
if(TARGET duckdb_icu_stubdata)
  target_sources(${EXTENSION_NAME} PRIVATE $<TARGET_OBJECTS:duckdb_icu_stubdata>)
  target_sources(${LOADABLE_EXTENSION_NAME} PRIVATE $<TARGET_OBJECTS:duckdb_icu_stubdata>)
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
