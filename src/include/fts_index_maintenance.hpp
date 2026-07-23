#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/parser/qualified_name.hpp"

namespace duckdb {

class FTSIndexMaintenance {
public:
  static string DropTriggers(const QualifiedName &qname);
  static string Create(const QualifiedName &qname, const string &input_id,
                       const vector<string> &input_values,
                       const string &stemmer, bool cluster_terms,
                       bool layered_search);
};

} // namespace duckdb
