#pragma once

#include "duckdb/common/common.hpp"

namespace duckdb {

struct SQLTemplateAsset {
  const char *source;
  const char *sql;
  const char *hash;
};

class SQLTemplateArgument {
public:
  static SQLTemplateArgument Identifier(const string &value);
  static SQLTemplateArgument QualifiedIdentifier(const vector<string> &parts);
  static SQLTemplateArgument StringLiteral(const string &value);
  static SQLTemplateArgument Integer(int64_t value);
  static SQLTemplateArgument TrustedSQL(const string &value);

  const string &GetSQL() const;

private:
  explicit SQLTemplateArgument(string sql);

  string sql;
};

using SQLTemplateBinding = std::pair<string, SQLTemplateArgument>;

string RenderSQLTemplate(const SQLTemplateAsset &asset,
                         const vector<SQLTemplateBinding> &bindings);

} // namespace duckdb
