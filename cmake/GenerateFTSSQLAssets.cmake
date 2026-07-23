function(_fts_write_if_different path content_variable)
  set(new_content "${${content_variable}}")
  if(EXISTS "${path}")
    file(READ "${path}" existing_content)
    if(existing_content STREQUAL new_content)
      return()
    endif()
  endif()
  file(WRITE "${path}" "${new_content}")
endfunction()

function(generate_fts_sql_assets output_directory)
  file(MAKE_DIRECTORY "${output_directory}")

  set(header_content "#pragma once\n\n#include \"fts_sql_template.hpp\"\n\nnamespace duckdb {\nnamespace fts_sql {\n\n")
  set(source_content "#include \"fts_sql_assets.hpp\"\n\nnamespace duckdb {\nnamespace fts_sql {\n\n")

  foreach(asset IN LISTS ARGN)
    string(REPLACE "|" ";" asset_parts "${asset}")
    list(LENGTH asset_parts asset_part_count)
    if(NOT asset_part_count EQUAL 2)
      message(FATAL_ERROR "Malformed FTS SQL asset '${asset}'")
    endif()
    list(GET asset_parts 0 asset_name)
    list(GET asset_parts 1 asset_relative_path)
    set(asset_path "${CMAKE_CURRENT_SOURCE_DIR}/${asset_relative_path}")
    if(NOT EXISTS "${asset_path}")
      message(FATAL_ERROR "FTS SQL asset does not exist: ${asset_path}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${asset_path}")
    file(READ "${asset_path}" asset_sql)
    string(REPLACE "\r\n" "\n" asset_sql "${asset_sql}")
    string(REPLACE "\r" "\n" asset_sql "${asset_sql}")
    string(SHA256 asset_hash "${asset_sql}")
    string(SUBSTRING "${asset_hash}" 0 12 asset_hash_prefix)
    set(raw_delimiter "fts_${asset_hash_prefix}")
    string(FIND "${asset_sql}" ")${raw_delimiter}\"" delimiter_collision)
    if(NOT delimiter_collision EQUAL -1)
      message(FATAL_ERROR
              "Raw string delimiter collision in FTS SQL asset ${asset_relative_path}")
    endif()

    string(APPEND header_content
           "extern const SQLTemplateAsset ${asset_name};\n")
    string(APPEND source_content
           "const SQLTemplateAsset ${asset_name} = {\n"
           "    \"${asset_relative_path}\",\n"
           "    R\"${raw_delimiter}(${asset_sql})${raw_delimiter}\",\n"
           "    \"${asset_hash}\"};\n\n")
  endforeach()

  string(APPEND header_content "\n} // namespace fts_sql\n} // namespace duckdb\n")
  string(APPEND source_content "} // namespace fts_sql\n} // namespace duckdb\n")

  set(header_path "${output_directory}/fts_sql_assets.hpp")
  set(source_path "${output_directory}/fts_sql_assets.cpp")
  _fts_write_if_different("${header_path}" header_content)
  _fts_write_if_different("${source_path}" source_content)

  set(FTS_SQL_ASSETS_HEADER "${header_path}" PARENT_SCOPE)
  set(FTS_SQL_ASSETS_SOURCE "${source_path}" PARENT_SCOPE)
endfunction()
