cmake_minimum_required(VERSION 2.8.12...3.29)
set(TARGET_NAME fts)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(FTSExtension)

set(EXTENSION_BASE_FOLDER "src")
set(SNOWBALL_BASE_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/third_party/snowball")
if(DEFINED DUCKDB_MODULE_BASE_DIR)
  set(DUCKDB_SOURCE_BASE_FOLDER "${DUCKDB_MODULE_BASE_DIR}")
else()
  set(DUCKDB_SOURCE_BASE_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/duckdb")
endif()
set(DUCKDB_ICU_BASE_FOLDER "${DUCKDB_SOURCE_BASE_FOLDER}/extension/icu")
include_directories(
    ${EXTENSION_BASE_FOLDER}/include
    ${SNOWBALL_BASE_FOLDER}/include
    ${SNOWBALL_BASE_FOLDER}/libstemmer
    ${SNOWBALL_BASE_FOLDER}/runtime
    ${DUCKDB_SOURCE_BASE_FOLDER}/third_party/utf8proc/include
    ${DUCKDB_ICU_BASE_FOLDER}/third_party/icu/common
    ${DUCKDB_ICU_BASE_FOLDER}/third_party/icu/i18n)

file(GLOB SNOWBALL_STEMMER_SOURCES CONFIGURE_DEPENDS ${SNOWBALL_BASE_FOLDER}/src_c/stem_*.c)

set(EXTENSION_SOURCES
    ${EXTENSION_BASE_FOLDER}/fts_extension.cpp
    ${EXTENSION_BASE_FOLDER}/fts_indexing.cpp
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
