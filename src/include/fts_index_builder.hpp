#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/parser/qualified_name.hpp"

namespace duckdb {

enum class FTSStopwordsMode : uint8_t { NONE, ENGLISH, TABLE };

struct FTSIndexConfig {
  QualifiedName input_table;
  string input_id;
  vector<string> input_values;
  string stemmer;
  FTSStopwordsMode stopwords_mode = FTSStopwordsMode::ENGLISH;
  QualifiedName stopwords_table;
  string tokenizer;
  string ignore;
  bool strip_accents = true;
  bool lower = true;
  bool incremental = false;
  bool cluster_terms = false;
  bool layered_search = false;
  bool structured_queries = false;
};

class FTSIndexBuilder {
public:
  static string Create(const FTSIndexConfig &config,
                       bool drop_existing_triggers);
  static string CreateStructuredQueryMacros(const QualifiedName &qname);
};

} // namespace duckdb
