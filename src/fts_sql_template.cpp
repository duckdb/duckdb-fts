#include "fts_sql_template.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/common/string_util.hpp"

#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace duckdb {

SQLTemplateArgument::SQLTemplateArgument(string sql_p)
    : sql(std::move(sql_p)) {}

SQLTemplateArgument SQLTemplateArgument::Identifier(const string &value) {
  return SQLTemplateArgument(SQLIdentifier::ToString(value));
}

SQLTemplateArgument
SQLTemplateArgument::QualifiedIdentifier(const vector<string> &parts) {
  if (parts.empty()) {
    throw InternalException("A qualified SQL identifier cannot be empty");
  }
  vector<string> quoted_parts;
  quoted_parts.reserve(parts.size());
  for (auto &part : parts) {
    quoted_parts.push_back(SQLIdentifier::ToString(part));
  }
  return SQLTemplateArgument(StringUtil::Join(quoted_parts, "."));
}

SQLTemplateArgument SQLTemplateArgument::StringLiteral(const string &value) {
  return SQLTemplateArgument(SQLString::ToString(value));
}

SQLTemplateArgument SQLTemplateArgument::Integer(int64_t value) {
  return SQLTemplateArgument(std::to_string(value));
}

SQLTemplateArgument SQLTemplateArgument::TrustedSQL(const string &value) {
  return SQLTemplateArgument(value);
}

const string &SQLTemplateArgument::GetSQL() const { return sql; }

static bool IsPlaceholderName(const string &name) {
  if (name.empty() ||
      !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
    return false;
  }
  for (idx_t index = 1; index < name.size(); index++) {
    auto character = static_cast<unsigned char>(name[index]);
    if (!(std::isalnum(character) || character == '_')) {
      return false;
    }
  }
  return true;
}

string RenderSQLTemplate(const SQLTemplateAsset &asset,
                         const vector<SQLTemplateBinding> &bindings) {
  std::unordered_map<string, const SQLTemplateArgument *> binding_map;
  for (auto &binding : bindings) {
    if (!IsPlaceholderName(binding.first)) {
      throw InternalException("Malformed binding '%s' for SQL template '%s'",
                              binding.first, asset.source);
    }
    if (!binding_map.emplace(binding.first, &binding.second).second) {
      throw InternalException("Duplicate binding '%s' for SQL template '%s'",
                              binding.first, asset.source);
    }
  }

  string result;
  std::unordered_set<string> used_bindings;
  const string template_sql(asset.sql);
  idx_t cursor = 0;
  while (cursor < template_sql.size()) {
    auto open = template_sql.find("{{", cursor);
    auto stray_close = template_sql.find("}}", cursor);
    if (stray_close != string::npos &&
        (open == string::npos || stray_close < open)) {
      throw InternalException("Malformed placeholder in SQL template '%s'",
                              asset.source);
    }
    if (open == string::npos) {
      result.append(template_sql, cursor, string::npos);
      break;
    }
    result.append(template_sql, cursor, open - cursor);
    auto close = template_sql.find("}}", open + 2);
    if (close == string::npos || template_sql.find("{{", open + 2) < close) {
      throw InternalException("Malformed placeholder in SQL template '%s'",
                              asset.source);
    }
    auto name = template_sql.substr(open + 2, close - open - 2);
    if (!IsPlaceholderName(name)) {
      throw InternalException(
          "Malformed placeholder '{{%s}}' in SQL template '%s'", name,
          asset.source);
    }
    auto binding = binding_map.find(name);
    if (binding == binding_map.end()) {
      throw InternalException("Missing binding '%s' for SQL template '%s'",
                              name, asset.source);
    }
    result += binding->second->GetSQL();
    used_bindings.insert(name);
    cursor = close + 2;
  }

  for (auto &binding : bindings) {
    if (used_bindings.find(binding.first) == used_bindings.end()) {
      throw InternalException("Unknown binding '%s' for SQL template '%s'",
                              binding.first, asset.source);
    }
  }
  return result;
}

} // namespace duckdb
