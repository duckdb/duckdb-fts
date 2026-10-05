#include "fts_query_parser.hpp"

#include "duckdb/common/vector/struct_vector.hpp"
#include "duckdb/common/vector/vector_writer.hpp"

namespace duckdb {

namespace {

enum class QueryTokenType : uint8_t {
  BAREWORD,
  STRING,
  KEYWORD_AND,
  KEYWORD_OR,
  KEYWORD_NOT,
  STAR,
  PLUS,
  COMMA,
  COLON,
  MINUS,
  CARET,
  LPAREN,
  RPAREN,
  LBRACE,
  RBRACE,
  END
};

struct QueryToken {
  QueryTokenType type;
  string text;
};

// FTS5 barewords admit ASCII alphanumerics, '_', U+001A and any non-ASCII byte.
static bool IsBarewordByte(char c) {
  auto b = static_cast<unsigned char>(c);
  return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') ||
         (b >= 'a' && b <= 'z') || b == '_' || b == 0x1A || b >= 0x80;
}

static string Lex(const string &input, vector<QueryToken> &tokens) {
  idx_t position = 0;
  while (position < input.size()) {
    auto c = input[position];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      position++;
      continue;
    }
    if (c == '"') {
      string text;
      position++;
      auto closed = false;
      while (position < input.size()) {
        if (input[position] == '"') {
          if (position + 1 < input.size() && input[position + 1] == '"') {
            text += '"';
            position += 2;
            continue;
          }
          position++;
          closed = true;
          break;
        }
        text += input[position];
        position++;
      }
      if (!closed) {
        return "fts5 query contains an unclosed string";
      }
      tokens.push_back({QueryTokenType::STRING, text});
      continue;
    }
    if (IsBarewordByte(c)) {
      string text;
      while (position < input.size() && IsBarewordByte(input[position])) {
        text += input[position];
        position++;
      }
      auto type = QueryTokenType::BAREWORD;
      if (text == "AND") {
        type = QueryTokenType::KEYWORD_AND;
      } else if (text == "OR") {
        type = QueryTokenType::KEYWORD_OR;
      } else if (text == "NOT") {
        type = QueryTokenType::KEYWORD_NOT;
      }
      tokens.push_back({type, text});
      continue;
    }
    switch (c) {
    case '*':
      tokens.push_back({QueryTokenType::STAR, "*"});
      break;
    case '+':
      tokens.push_back({QueryTokenType::PLUS, "+"});
      break;
    case ',':
      tokens.push_back({QueryTokenType::COMMA, ","});
      break;
    case ':':
      tokens.push_back({QueryTokenType::COLON, ":"});
      break;
    case '-':
      tokens.push_back({QueryTokenType::MINUS, "-"});
      break;
    case '^':
      tokens.push_back({QueryTokenType::CARET, "^"});
      break;
    case '(':
      tokens.push_back({QueryTokenType::LPAREN, "("});
      break;
    case ')':
      tokens.push_back({QueryTokenType::RPAREN, ")"});
      break;
    case '{':
      tokens.push_back({QueryTokenType::LBRACE, "{"});
      break;
    case '}':
      tokens.push_back({QueryTokenType::RBRACE, "}"});
      break;
    default:
      return "fts5 query contains a character that requires quoting";
    }
    position++;
  }
  tokens.push_back({QueryTokenType::END, ""});
  return string();
}

// FTS5 itself bounds nesting (its error names a maximum depth of 256); the
// same bound keeps the recursive descent and the serializer within the stack.
static constexpr idx_t MAX_QUERY_DEPTH = 256;

enum class QueryNodeType : uint8_t { LEAF, GROUP_AND, GROUP_OR, GROUP_NOT };

struct QueryNode {
  QueryNodeType type = QueryNodeType::LEAF;
  string query;
  string query_mode;
  string near_distance;
  vector<string> fields;
  bool grouped = false;
  vector<unique_ptr<QueryNode>> children;
};

static string EscapeJSON(const string &input) {
  string result;
  for (auto c : input) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        result += StringUtil::Format("\\u%04x", c);
      } else {
        result += c;
      }
    }
  }
  return result;
}

struct QueryParser {
  const vector<QueryToken> &tokens;
  idx_t position = 0;
  idx_t depth = 0;
  string error;

  explicit QueryParser(const vector<QueryToken> &tokens) : tokens(tokens) {}

  const QueryToken &Peek(idx_t offset = 0) const {
    auto index = MinValue<idx_t>(position + offset, tokens.size() - 1);
    return tokens[index];
  }

  const QueryToken &Next() {
    auto &token = tokens[position];
    if (position + 1 < tokens.size()) {
      position++;
    }
    return token;
  }

  bool StartsPrimary(const QueryToken &token) const {
    switch (token.type) {
    case QueryTokenType::BAREWORD:
    case QueryTokenType::STRING:
    case QueryTokenType::LBRACE:
    case QueryTokenType::MINUS:
    case QueryTokenType::CARET:
    case QueryTokenType::LPAREN:
      return true;
    default:
      return false;
    }
  }

  unique_ptr<QueryNode> ParseOr(const vector<string> &fields);
  unique_ptr<QueryNode> ParseAnd(const vector<string> &fields);
  unique_ptr<QueryNode> ParseNot(const vector<string> &fields);
  unique_ptr<QueryNode> ParseImplicit(const vector<string> &fields);
  unique_ptr<QueryNode> ParsePrimary(const vector<string> &fields);
  unique_ptr<QueryNode> ParseNear(const vector<string> &fields);
  unique_ptr<QueryNode> ParsePhrase(const vector<string> &fields);
};

unique_ptr<QueryNode> QueryParser::ParseOr(const vector<string> &fields) {
  auto left = ParseAnd(fields);
  if (!left) {
    return nullptr;
  }
  while (Peek().type == QueryTokenType::KEYWORD_OR) {
    Next();
    auto right = ParseAnd(fields);
    if (!right) {
      return nullptr;
    }
    if (left->type == QueryNodeType::GROUP_OR && !left->grouped) {
      left->children.push_back(std::move(right));
    } else {
      auto group = make_uniq<QueryNode>();
      group->type = QueryNodeType::GROUP_OR;
      group->children.push_back(std::move(left));
      group->children.push_back(std::move(right));
      left = std::move(group);
    }
  }
  return left;
}

unique_ptr<QueryNode> QueryParser::ParseAnd(const vector<string> &fields) {
  auto left = ParseNot(fields);
  if (!left) {
    return nullptr;
  }
  while (Peek().type == QueryTokenType::KEYWORD_AND) {
    Next();
    auto right = ParseNot(fields);
    if (!right) {
      return nullptr;
    }
    if (left->type == QueryNodeType::GROUP_AND && !left->grouped) {
      left->children.push_back(std::move(right));
    } else {
      auto group = make_uniq<QueryNode>();
      group->type = QueryNodeType::GROUP_AND;
      group->children.push_back(std::move(left));
      group->children.push_back(std::move(right));
      left = std::move(group);
    }
  }
  return left;
}

unique_ptr<QueryNode> QueryParser::ParseNot(const vector<string> &fields) {
  auto left = ParseImplicit(fields);
  if (!left) {
    return nullptr;
  }
  while (Peek().type == QueryTokenType::KEYWORD_NOT) {
    // NOT chains deepen the final tree permanently, so this is not refunded.
    if (++depth > MAX_QUERY_DEPTH) {
      error = "fts5 query is nested too deeply";
      return nullptr;
    }
    Next();
    auto right = ParseImplicit(fields);
    if (!right) {
      return nullptr;
    }
    auto group = make_uniq<QueryNode>();
    group->type = QueryNodeType::GROUP_NOT;
    group->children.push_back(std::move(left));
    group->children.push_back(std::move(right));
    left = std::move(group);
  }
  return left;
}

unique_ptr<QueryNode> QueryParser::ParseImplicit(const vector<string> &fields) {
  auto left = ParsePrimary(fields);
  if (!left) {
    return nullptr;
  }
  while (StartsPrimary(Peek())) {
    // FTS5 never inserts an implicit AND next to a parenthesised group.
    if (Peek().type == QueryTokenType::LPAREN || left->grouped) {
      error = "fts5 query requires an operator next to a parenthesised group";
      return nullptr;
    }
    auto right = ParsePrimary(fields);
    if (!right) {
      return nullptr;
    }
    if (left->type == QueryNodeType::GROUP_AND && !left->grouped) {
      left->children.push_back(std::move(right));
    } else {
      auto group = make_uniq<QueryNode>();
      group->type = QueryNodeType::GROUP_AND;
      group->children.push_back(std::move(left));
      group->children.push_back(std::move(right));
      left = std::move(group);
    }
  }
  return left;
}

unique_ptr<QueryNode> QueryParser::ParsePrimary(const vector<string> &fields) {
  auto &token = Peek();
  if (token.type == QueryTokenType::MINUS) {
    error = "fts5 column complements are not supported";
    return nullptr;
  }
  if (token.type == QueryTokenType::CARET) {
    error = "fts5 initial-token anchors are not supported";
    return nullptr;
  }
  if (token.type == QueryTokenType::LBRACE) {
    Next();
    vector<string> names;
    while (Peek().type == QueryTokenType::BAREWORD) {
      names.push_back(Next().text);
    }
    if (Peek().type != QueryTokenType::RBRACE || names.empty()) {
      error = "fts5 column list is invalid";
      return nullptr;
    }
    Next();
    if (Peek().type != QueryTokenType::COLON) {
      error = "fts5 column list must be followed by a colon";
      return nullptr;
    }
    Next();
    vector<string> narrowed;
    for (auto &name : names) {
      if (fields.empty() ||
          std::find(fields.begin(), fields.end(), name) != fields.end()) {
        narrowed.push_back(name);
      }
    }
    if (narrowed.empty()) {
      error = "fts5 column filters select no common column";
      return nullptr;
    }
    if (++depth > MAX_QUERY_DEPTH) {
      error = "fts5 query is nested too deeply";
      return nullptr;
    }
    auto inner = ParsePrimary(narrowed);
    depth--;
    return inner;
  }
  if (token.type == QueryTokenType::BAREWORD &&
      Peek(1).type == QueryTokenType::COLON) {
    auto name = Next().text;
    Next();
    vector<string> narrowed;
    if (fields.empty() ||
        std::find(fields.begin(), fields.end(), name) != fields.end()) {
      narrowed.push_back(name);
    }
    if (narrowed.empty()) {
      error = "fts5 column filters select no common column";
      return nullptr;
    }
    if (++depth > MAX_QUERY_DEPTH) {
      error = "fts5 query is nested too deeply";
      return nullptr;
    }
    auto inner = ParsePrimary(narrowed);
    depth--;
    return inner;
  }
  if (token.type == QueryTokenType::LPAREN) {
    Next();
    if (++depth > MAX_QUERY_DEPTH) {
      error = "fts5 query is nested too deeply";
      return nullptr;
    }
    auto inner = ParseOr(fields);
    depth--;
    if (!inner) {
      return nullptr;
    }
    if (Peek().type != QueryTokenType::RPAREN) {
      error = "fts5 query contains an unclosed parenthesis";
      return nullptr;
    }
    Next();
    inner->grouped = true;
    return inner;
  }
  if (token.type == QueryTokenType::BAREWORD && token.text == "NEAR" &&
      Peek(1).type == QueryTokenType::LPAREN) {
    return ParseNear(fields);
  }
  if (token.type == QueryTokenType::BAREWORD ||
      token.type == QueryTokenType::STRING) {
    return ParsePhrase(fields);
  }
  error = "fts5 query has a dangling operator";
  return nullptr;
}

unique_ptr<QueryNode> QueryParser::ParseNear(const vector<string> &fields) {
  Next();
  Next();
  vector<string> terms;
  while (Peek().type == QueryTokenType::BAREWORD) {
    terms.push_back(Next().text);
    if (Peek().type == QueryTokenType::STAR) {
      error = "fts5 prefixes inside NEAR are not supported";
      return nullptr;
    }
  }
  if (Peek().type == QueryTokenType::STRING) {
    error = "fts5 phrases inside NEAR are not supported";
    return nullptr;
  }
  if (terms.size() < 2) {
    error = "fts5 NEAR requires two or more terms";
    return nullptr;
  }
  string near_distance;
  if (Peek().type == QueryTokenType::COMMA) {
    Next();
    auto &number = Peek();
    if (number.type != QueryTokenType::BAREWORD ||
        number.text.find_first_not_of("0123456789") != string::npos) {
      error = "fts5 NEAR distance must be a non-negative integer";
      return nullptr;
    }
    // Re-serialized through an integer: raw digits like 007 are invalid JSON.
    uint64_t distance = 0;
    auto maximum = static_cast<uint64_t>(NumericLimits<int64_t>::Maximum());
    for (auto c : number.text) {
      auto digit = static_cast<uint64_t>(c - '0');
      if (distance > (maximum - digit) / 10) {
        error = "fts5 NEAR distance is out of range";
        return nullptr;
      }
      distance = distance * 10 + digit;
    }
    Next();
    near_distance = to_string(distance);
  }
  if (Peek().type != QueryTokenType::RPAREN) {
    error = "fts5 NEAR group is unclosed";
    return nullptr;
  }
  Next();
  auto leaf = make_uniq<QueryNode>();
  leaf->query = StringUtil::Join(terms, " ");
  leaf->query_mode = "near";
  leaf->near_distance = near_distance;
  leaf->fields = fields;
  return leaf;
}

unique_ptr<QueryNode> QueryParser::ParsePhrase(const vector<string> &fields) {
  vector<string> parts;
  auto quoted = false;
  auto prefix = false;
  while (true) {
    auto &token = Peek();
    if (token.type == QueryTokenType::BAREWORD) {
      parts.push_back(Next().text);
    } else if (token.type == QueryTokenType::STRING) {
      quoted = true;
      parts.push_back(Next().text);
    } else {
      error = "fts5 query has a dangling operator";
      return nullptr;
    }
    if (Peek().type == QueryTokenType::STAR) {
      Next();
      prefix = true;
      break;
    }
    if (Peek().type != QueryTokenType::PLUS) {
      break;
    }
    Next();
  }
  // FTS5 lets "" concatenate as a no-op, so empty segments drop out.
  vector<string> words;
  for (auto &part : parts) {
    if (!part.empty()) {
      words.push_back(part);
    }
  }
  auto leaf = make_uniq<QueryNode>();
  leaf->query = StringUtil::Join(words, " ");
  leaf->fields = fields;
  auto multiple_tokens = quoted || words.size() > 1;
  if (prefix) {
    leaf->query_mode = multiple_tokens ? "phrase_prefix" : "autocomplete";
  } else if (multiple_tokens) {
    leaf->query_mode = "phrase";
  }
  return leaf;
}

// FTS5 treats an empty phrase as adding no constraint: it drops out of AND, OR
// and NOT groups, while a query left with no constraint at all matches nothing.
static unique_ptr<QueryNode> PruneNeutralNodes(unique_ptr<QueryNode> node) {
  if (node->type == QueryNodeType::LEAF) {
    return node->query.empty() ? nullptr : std::move(node);
  }
  if (node->type == QueryNodeType::GROUP_NOT) {
    auto must = PruneNeutralNodes(std::move(node->children[0]));
    auto must_not = PruneNeutralNodes(std::move(node->children[1]));
    if (!must) {
      // NOT without a positive side matches nothing, like a bare empty phrase.
      auto empty = make_uniq<QueryNode>();
      empty->query_mode = "phrase";
      return empty;
    }
    if (!must_not) {
      return must;
    }
    node->children[0] = std::move(must);
    node->children[1] = std::move(must_not);
    return node;
  }
  vector<unique_ptr<QueryNode>> kept;
  for (auto &child : node->children) {
    auto pruned = PruneNeutralNodes(std::move(child));
    if (pruned) {
      kept.push_back(std::move(pruned));
    }
  }
  if (kept.empty()) {
    return nullptr;
  }
  if (kept.size() == 1) {
    return std::move(kept[0]);
  }
  node->children = std::move(kept);
  return node;
}

static void SerializeNode(const QueryNode &node, string &result) {
  result += "{";
  if (node.type == QueryNodeType::LEAF) {
    result += "\"query\":\"" + EscapeJSON(node.query) + "\"";
    if (!node.query_mode.empty()) {
      result += ",\"query_mode\":\"" + node.query_mode + "\"";
    }
    if (!node.near_distance.empty()) {
      result += ",\"near_distance\":" + node.near_distance;
    }
    if (!node.fields.empty()) {
      result += ",\"fields\":[";
      for (idx_t i = 0; i < node.fields.size(); i++) {
        if (i > 0) {
          result += ",";
        }
        result += "\"" + EscapeJSON(node.fields[i]) + "\"";
      }
      result += "]";
    }
  } else if (node.type == QueryNodeType::GROUP_NOT) {
    result += "\"must\":[";
    SerializeNode(*node.children[0], result);
    result += "],\"must_not\":[";
    SerializeNode(*node.children[1], result);
    result += "]";
  } else {
    result +=
        node.type == QueryNodeType::GROUP_AND ? "\"must\":[" : "\"should\":[";
    for (idx_t i = 0; i < node.children.size(); i++) {
      if (i > 0) {
        result += ",";
      }
      SerializeNode(*node.children[i], result);
    }
    result += "]";
  }
  result += "}";
}

static string ParseQuery(const string &input, string &query_json) {
  vector<QueryToken> tokens;
  auto lex_error = Lex(input, tokens);
  if (!lex_error.empty()) {
    return lex_error;
  }
  if (tokens.size() == 1) {
    return "fts5 query is empty";
  }
  QueryParser parser(tokens);
  auto root = parser.ParseOr(vector<string>());
  if (!root) {
    return parser.error.empty() ? "fts5 query is invalid" : parser.error;
  }
  if (parser.Peek().type != QueryTokenType::END) {
    return parser.error.empty() ? "fts5 query has trailing input"
                                : parser.error;
  }
  root = PruneNeutralNodes(std::move(root));
  if (!root) {
    root = make_uniq<QueryNode>();
    root->query_mode = "phrase";
  }
  SerializeNode(*root, query_json);
  return string();
}

} // namespace

static void ParseQueryFunction(DataChunk &args, ExpressionState &state,
                               Vector &result) {
  auto inputs = args.data[0].Values<string_t>();

  result.SetVectorType(VectorType::FLAT_VECTOR);
  FlatVector::ValidityMutable(result).SetAllValid(args.size());
  auto &children = StructVector::GetEntries(result);
  auto json_writer = FlatVector::Writer<string_t>(children[0], args.size());
  auto error_writer = FlatVector::Writer<string_t>(children[1], args.size());

  for (idx_t i = 0; i < args.size(); i++) {
    auto input = inputs[i];
    string query_json;
    string error;
    if (!input.IsValid()) {
      error = "fts5 query must not be NULL";
    } else {
      error = ParseQuery(input.GetValue().GetString(), query_json);
    }
    if (error.empty()) {
      json_writer.WriteValue(query_json);
      error_writer.WriteNull();
    } else {
      json_writer.WriteNull();
      error_writer.WriteValue(error);
    }
  }
}

ScalarFunction GetFTSParseQueryFunction() {
  auto return_type =
      LogicalType::STRUCT({{"query_json", LogicalType::VARCHAR},
                           {"error_message", LogicalType::VARCHAR}});
  ScalarFunction function("fts_parse_query", {LogicalType::VARCHAR},
                          return_type, ParseQueryFunction);
  function.SetNullHandling(FunctionNullHandling::SPECIAL_HANDLING);
  return function;
}

} // namespace duckdb
