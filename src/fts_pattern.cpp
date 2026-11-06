#include "fts_pattern.hpp"

#include "duckdb/common/string_util.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include "duckdb/common/vector/vector_writer.hpp"
#include "re2/re2.h"
#include "utf8proc_wrapper.hpp"

namespace duckdb {

struct PatternToken {
  string literal;
  bool is_literal;
  bool is_quantifier;
};

struct PatternAnalysis {
  string verification_pattern;
  string lookup_kind;
  string lookup_literal;
  string error_message;
};

static idx_t UTF8CharacterSize(const string &input, idx_t position) {
  int char_size = 0;
  Utf8Proc::UTF8ToCodepoint(input.data() + position, char_size,
                            input.size() - position);
  return char_size > 0 ? UnsafeNumericCast<idx_t>(char_size) : 0;
}

static bool IsASCIIAlphanumeric(const string &input, idx_t position,
                                idx_t char_size) {
  if (char_size != 1) {
    return false;
  }
  auto c = input[position];
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

static string QuoteRegexLiteral(const string &literal) {
  return duckdb_re2::RE2::QuoteMeta(literal);
}

static bool ParseBoundedInteger(const string &input, idx_t begin, idx_t end,
                                idx_t &value) {
  if (begin == end) {
    return false;
  }
  value = 0;
  for (auto position = begin; position < end; position++) {
    auto c = input[position];
    if (c < '0' || c > '9') {
      return false;
    }
    if (value < 1001) {
      value =
          MinValue<idx_t>(1001, value * 10 + UnsafeNumericCast<idx_t>(c - '0'));
    }
  }
  return true;
}

static bool ParseWildcard(const string &pattern, vector<PatternToken> &tokens,
                          string &verification_pattern, string &error_message) {
  for (idx_t position = 0; position < pattern.size();) {
    auto char_size = UTF8CharacterSize(pattern, position);
    if (char_size == 0) {
      error_message = "wildcard pattern is invalid UTF-8";
      return false;
    }
    auto token = pattern.substr(position, char_size);
    if (char_size == 1 && pattern[position] == '\\') {
      position += char_size;
      if (position == pattern.size()) {
        error_message =
            "wildcard pattern must not end with an escape character";
        return false;
      }
      char_size = UTF8CharacterSize(pattern, position);
      if (char_size == 0) {
        error_message = "wildcard pattern is invalid UTF-8";
        return false;
      }
      token = pattern.substr(position, char_size);
      verification_pattern += QuoteRegexLiteral(token);
      tokens.push_back({token, true, false});
    } else if (char_size == 1 && pattern[position] == '*') {
      verification_pattern += ".*";
      tokens.push_back({string(), false, false});
    } else if (char_size == 1 && pattern[position] == '?') {
      verification_pattern += ".";
      tokens.push_back({string(), false, false});
    } else {
      verification_pattern += QuoteRegexLiteral(token);
      tokens.push_back({token, true, false});
    }
    position += char_size;
  }
  return true;
}

static string ParseRegex(const string &pattern, vector<PatternToken> &tokens,
                         string &error_message) {
  for (idx_t position = 0; position < pattern.size();) {
    auto char_size = UTF8CharacterSize(pattern, position);
    if (char_size == 0) {
      error_message = "regex pattern is invalid";
      return string();
    }
    auto c = pattern[position];
    if (char_size == 1 && c == '\\') {
      auto escaped_position = position + 1;
      if (escaped_position == pattern.size()) {
        error_message = "regex pattern must not end with an escape character";
        return string();
      }
      auto escaped_size = UTF8CharacterSize(pattern, escaped_position);
      if (escaped_size == 0) {
        error_message = "regex pattern is invalid";
        return string();
      }
      if (IsASCIIAlphanumeric(pattern, escaped_position, escaped_size)) {
        error_message = "regex pattern contains an unsupported escape sequence";
        return string();
      }
      tokens.push_back(
          {pattern.substr(escaped_position, escaped_size), true, false});
      position = escaped_position + escaped_size;
      continue;
    }
    if (char_size == 1 && c == '[') {
      auto class_position = position + 1;
      bool has_content = false;
      bool closed = false;
      bool class_has_unsupported_escape = false;
      bool class_has_nested_open = false;
      while (class_position < pattern.size()) {
        auto class_char_size = UTF8CharacterSize(pattern, class_position);
        if (class_char_size == 0) {
          error_message = "regex pattern is invalid";
          return string();
        }
        auto class_char = pattern[class_position];
        if (class_char_size == 1 && class_char == '\\') {
          auto escaped_position = class_position + 1;
          if (escaped_position == pattern.size()) {
            error_message = "regex pattern contains an invalid character class";
            return string();
          }
          auto escaped_size = UTF8CharacterSize(pattern, escaped_position);
          if (escaped_size == 0) {
            error_message = "regex pattern is invalid";
            return string();
          }
          if (IsASCIIAlphanumeric(pattern, escaped_position, escaped_size)) {
            class_has_unsupported_escape = true;
          }
          if (escaped_size == 1 && pattern[escaped_position] == '[') {
            class_has_nested_open = true;
          }
          has_content = true;
          class_position = escaped_position + escaped_size;
          continue;
        }
        if (class_char_size == 1 && class_char == '[') {
          class_has_nested_open = true;
        }
        if (class_char_size == 1 && class_char == ']') {
          closed = has_content;
          class_position++;
          break;
        }
        has_content = true;
        class_position += class_char_size;
      }
      if (!closed) {
        error_message = "regex pattern contains an invalid character class";
        return string();
      }
      if (class_has_unsupported_escape) {
        error_message = "regex character classes contain an unsupported "
                        "escape sequence";
        return string();
      }
      if (class_has_nested_open) {
        error_message =
            "regex pattern contains unsupported character class syntax";
        return string();
      }
      tokens.push_back({string(), false, false});
      position = class_position;
      continue;
    }
    if (char_size == 1 && (c == '*' || c == '+' || c == '?')) {
      if (tokens.empty() || tokens.back().is_quantifier) {
        error_message = "regex pattern contains a quantifier without an atom";
        return string();
      }
      tokens.push_back({string(), false, true});
      position++;
      continue;
    }
    if (char_size == 1 && c == '{') {
      if (tokens.empty() || tokens.back().is_quantifier) {
        error_message = "regex pattern contains a quantifier without an atom";
        return string();
      }
      auto close = pattern.find('}', position + 1);
      if (close == string::npos) {
        error_message = "regex pattern contains an invalid quantifier";
        return string();
      }
      auto comma = pattern.find(',', position + 1);
      if (comma != string::npos && comma > close) {
        comma = string::npos;
      }
      idx_t minimum = 0;
      auto minimum_end = comma == string::npos ? close : comma;
      if (!ParseBoundedInteger(pattern, position + 1, minimum_end, minimum)) {
        error_message = "regex pattern contains an invalid quantifier";
        return string();
      }
      if (minimum > 1000) {
        error_message =
            "regex quantifier exceeds the supported repetition limit";
        return string();
      }
      if (comma != string::npos && comma + 1 < close) {
        idx_t maximum = 0;
        if (!ParseBoundedInteger(pattern, comma + 1, close, maximum) ||
            maximum > 1000 || minimum > maximum) {
          error_message = "regex pattern contains an invalid quantifier range";
          return string();
        }
      }
      tokens.push_back({string(), false, true});
      position = close + 1;
      continue;
    }
    if (char_size == 1 &&
        (c == '(' || c == ')' || c == '|' || c == '^' || c == '$')) {
      error_message = "regex pattern contains unsupported syntax";
      return string();
    }
    if (char_size == 1 && (c == ']' || c == '}')) {
      error_message = "regex pattern contains a quantifier without an atom";
      return string();
    }
    if (char_size == 1 && c == '.') {
      tokens.push_back({string(), false, false});
    } else {
      tokens.push_back({pattern.substr(position, char_size), true, false});
    }
    position += char_size;
  }
  return pattern;
}

static void SelectLookupLiteral(const vector<PatternToken> &tokens,
                                PatternAnalysis &analysis) {
  string run;
  idx_t run_length = 0;
  idx_t run_start = 0;
  bool found_internal = false;
  idx_t selected_internal_length = 0;

  auto finish_run = [&]() {
    if (run_length == 0) {
      return false;
    }
    if (run_start == 0 && run_length >= 2) {
      analysis.lookup_kind = "prefix";
      analysis.lookup_literal = run;
      return true;
    }
    if (run_length >= 3 &&
        (!found_internal || run_length > selected_internal_length)) {
      analysis.lookup_kind = "gram";
      analysis.lookup_literal = run;
      selected_internal_length = run_length;
      found_internal = true;
    }
    run.clear();
    run_length = 0;
    return false;
  };

  for (idx_t token_index = 0; token_index < tokens.size(); token_index++) {
    auto mandatory = tokens[token_index].is_literal &&
                     (token_index + 1 == tokens.size() ||
                      !tokens[token_index + 1].is_quantifier);
    if (!mandatory) {
      if (finish_run()) {
        return;
      }
      continue;
    }
    if (run_length == 0) {
      run_start = token_index;
    }
    run += tokens[token_index].literal;
    run_length++;
  }
  finish_run();
}

static PatternAnalysis AnalyzePattern(const string &pattern,
                                      const string &pattern_mode) {
  PatternAnalysis analysis;
  vector<PatternToken> tokens;

  auto mode = StringUtil::Lower(pattern_mode);
  if (pattern.empty()) {
    analysis.error_message = "pattern must not be empty";
    return analysis;
  }
  if (mode == "wildcard") {
    if (!ParseWildcard(pattern, tokens, analysis.verification_pattern,
                       analysis.error_message)) {
      return analysis;
    }
  } else if (mode == "regex") {
    analysis.verification_pattern =
        ParseRegex(pattern, tokens, analysis.error_message);
    if (!analysis.error_message.empty()) {
      return analysis;
    }
  } else {
    analysis.error_message = "pattern mode must be wildcard or regex";
    return analysis;
  }

  SelectLookupLiteral(tokens, analysis);
  if (analysis.lookup_literal.empty()) {
    analysis.error_message = "pattern must contain a leading literal prefix "
                             "of at least two characters or a mandatory "
                             "literal run of at least three characters";
    return analysis;
  }
  duckdb_re2::RE2::Options regex_options;
  regex_options.set_log_errors(false);
  duckdb_re2::RE2 regex(analysis.verification_pattern, regex_options);
  if (!regex.ok()) {
    analysis.lookup_kind.clear();
    analysis.lookup_literal.clear();
    analysis.error_message = "regex pattern is invalid";
  }
  return analysis;
}

static void AnalyzePatternFunction(DataChunk &args, ExpressionState &state,
                                   Vector &result) {
  UnifiedVectorFormat pattern_data;
  UnifiedVectorFormat mode_data;
  args.data[0].ToUnifiedFormat(pattern_data);
  args.data[1].ToUnifiedFormat(mode_data);
  auto patterns = UnifiedVectorFormat::GetData<string_t>(pattern_data);
  auto modes = UnifiedVectorFormat::GetData<string_t>(mode_data);

  result.SetVectorType(VectorType::FLAT_VECTOR);
  FlatVector::ValidityMutable(result).SetAllValid(args.size());
  auto &children = StructVector::GetEntries(result);
  auto verification_writer =
      FlatVector::Writer<string_t>(children[0], args.size());
  auto kind_writer = FlatVector::Writer<string_t>(children[1], args.size());
  auto literal_writer = FlatVector::Writer<string_t>(children[2], args.size());
  auto error_writer = FlatVector::Writer<string_t>(children[3], args.size());

  for (idx_t row = 0; row < args.size(); row++) {
    auto pattern_index = pattern_data.sel->get_index(row);
    auto mode_index = mode_data.sel->get_index(row);
    PatternAnalysis analysis;
    if (!pattern_data.validity.RowIsValid(pattern_index)) {
      analysis.error_message = "pattern must not be NULL";
    } else if (!mode_data.validity.RowIsValid(mode_index)) {
      analysis.error_message = "pattern mode must be wildcard or regex";
    } else {
      analysis = AnalyzePattern(patterns[pattern_index].GetString(),
                                modes[mode_index].GetString());
    }

    if (analysis.error_message.empty()) {
      verification_writer.WriteValue(analysis.verification_pattern);
      kind_writer.WriteValue(analysis.lookup_kind);
      literal_writer.WriteValue(analysis.lookup_literal);
      error_writer.WriteNull();
    } else {
      verification_writer.WriteValue(string());
      kind_writer.WriteNull();
      literal_writer.WriteNull();
      error_writer.WriteValue(analysis.error_message);
    }
  }
}

ScalarFunction GetFTSAnalyzePatternFunction() {
  auto return_type =
      LogicalType::STRUCT({{"verification_pattern", LogicalType::VARCHAR},
                           {"lookup_kind", LogicalType::VARCHAR},
                           {"lookup_literal", LogicalType::VARCHAR},
                           {"error_message", LogicalType::VARCHAR}});
  ScalarFunction function("fts_analyze_pattern",
                          {LogicalType::VARCHAR, LogicalType::VARCHAR},
                          return_type, AnalyzePatternFunction);
  function.SetNullHandling(FunctionNullHandling::SPECIAL_HANDLING);
  function.SetFallible();
  return function;
}

} // namespace duckdb
