#include "fts_tokenizer.hpp"

#include "duckdb/common/string_util.hpp"
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include "duckdb/common/vector/vector_writer.hpp"
#include "duckdb/execution/expression_executor_state.hpp"
#include "fts_opensearch_tokenizer.hpp"
#include "re2/re2.h"
#include "utf8proc.hpp"
#include "utf8proc_wrapper.hpp"

#include <type_traits>

namespace duckdb {

namespace {

// One entry per normalized codepoint or contiguous run with unchanged byte
// widths.
struct NormalizedPiece {
  uint32_t norm_start;
  uint32_t norm_size;
  uint32_t orig_start;
  uint32_t orig_size;
};

struct NormalizedText {
  string text;
  vector<NormalizedPiece> pieces;
  idx_t source_size;
};

static bool IsASCII(const char *data, idx_t size) {
  for (idx_t i = 0; i < size; i++) {
    if (data[i] & 0x80) {
      return false;
    }
  }
  return true;
}

template <bool WITH_OFFSETS>
static NormalizedText NormalizeWithMap(const char *data, idx_t size,
                                       bool strip_accents, bool lower) {
  NormalizedText result;
  result.source_size = size;
  if (IsASCII(data, size)) {
    // ASCII normalization preserves every byte boundary. Copy/fold the entire
    // string rather than decoding and encoding each byte as a codepoint.
    result.text.assign(data, size);
    if (lower) {
      for (auto &c : result.text) {
        c = StringUtil::CharacterToLower(c);
      }
    }
    if (WITH_OFFSETS && size > 0) {
      result.pieces.push_back({0, UnsafeNumericCast<uint32_t>(size), 0,
                               UnsafeNumericCast<uint32_t>(size)});
    }
    return result;
  }
  // Analyzer-version compatibility: SQL strip_accents truncates non-ASCII
  // inputs at NUL, even when the non-ASCII character follows that NUL.
  if (strip_accents) {
    auto nul = static_cast<const char *>(memchr(data, 0, size));
    if (nul) {
      size = UnsafeNumericCast<idx_t>(nul - data);
      result.source_size = size;
    }
  }
  result.text.reserve(size);
  const auto options = utf8proc_option_t(UTF8PROC_STABLE | UTF8PROC_COMPOSE |
                                         UTF8PROC_STRIPMARK);
  int32_t pending = -1;
  uint32_t pending_start = 0;
  uint32_t pending_end = 0;

  auto flush = [&]() {
    if (pending < 0) {
      return;
    }
    auto codepoint = lower ? Utf8Proc::CodepointToLower(pending) : pending;
    char encoded[4];
    int encoded_size = 0;
    if (!Utf8Proc::CodepointToUtf8(codepoint, encoded_size, encoded)) {
      throw InvalidInputException("Invalid codepoint in FTS normalization");
    }
    auto width = UnsafeNumericCast<uint32_t>(encoded_size);
    if (WITH_OFFSETS) {
      bool extended = false;
      if (!result.pieces.empty()) {
        auto &run = result.pieces.back();
        if (run.norm_size == run.orig_size &&
            width == pending_end - pending_start &&
            run.orig_start + run.orig_size == pending_start) {
          run.norm_size += width;
          run.orig_size += width;
          extended = true;
        }
      }
      if (!extended) {
        result.pieces.push_back(
            {UnsafeNumericCast<uint32_t>(result.text.size()), width,
             pending_start, pending_end - pending_start});
      }
    }
    result.text.append(encoded, width);
  };

  auto append = [&](int32_t codepoint, uint32_t start, uint32_t end) {
    if (strip_accents) {
      // STRIPMARK removes every nonzero combining class, so no canonical
      // reordering remains. Let utf8proc compose adjacent starters.
      D_ASSERT(utf8proc_get_property(codepoint)->combining_class == 0);
      if (pending >= 0 && codepoint >= 128) {
        int32_t pair[] = {pending, codepoint};
        if (utf8proc_normalize_utf32(pair, 2, options) == 1) {
          pending = pair[0];
          pending_end = end;
          return;
        }
      }
    }
    flush();
    pending = codepoint;
    pending_start = start;
    pending_end = end;
  };

  vector<int32_t> decomposition;
  for (idx_t pos = 0; pos < size;) {
    int char_size = 1;
    auto codepoint = int32_t(static_cast<unsigned char>(data[pos]));
    if (codepoint >= 128) {
      codepoint = Utf8Proc::UTF8ToCodepoint(data + pos, char_size, size - pos);
      if (codepoint < 0 || char_size <= 0) {
        throw InvalidInputException("Invalid UTF-8 in FTS input");
      }
    }
    auto start = UnsafeNumericCast<uint32_t>(pos);
    pos += UnsafeNumericCast<idx_t>(char_size);
    auto end = UnsafeNumericCast<uint32_t>(pos);
    if (!strip_accents || codepoint < 128) {
      append(codepoint, start, end);
      continue;
    }
    int32_t buffer[4];
    auto count =
        utf8proc_decompose_char(codepoint, buffer, 4, options, nullptr);
    if (count < 0) {
      throw InvalidInputException(utf8proc_errmsg(count));
    }
    if (count == 0) {
      // A removed mark still belongs to the original span of its base.
      pending_end = end;
      continue;
    }
    auto decomposed = buffer;
    if (count > 4) {
      decomposition.resize(UnsafeNumericCast<idx_t>(count));
      utf8proc_decompose_char(codepoint, decomposition.data(), count, options,
                              nullptr);
      decomposed = decomposition.data();
    }
    for (utf8proc_ssize_t i = 0; i < count; i++) {
      append(decomposed[i], start, end);
    }
  }
  flush();
  return result;
}

static const NormalizedPiece *PieceContaining(const NormalizedText &normalized,
                                              uint32_t norm_position) {
  auto it = std::upper_bound(normalized.pieces.begin(), normalized.pieces.end(),
                             norm_position,
                             [](uint32_t value, const NormalizedPiece &piece) {
                               return value < piece.norm_start;
                             });
  if (it == normalized.pieces.begin()) {
    return nullptr;
  }
  return &*(it - 1);
}

struct TokenSpan {
  uint32_t norm_start;
  uint32_t norm_size;
};

static bool IsUtf8CharacterStart(char c) { return (c & 0xC0) != 0x80; }

struct CachedRegex {
  string delimiter;
  unique_ptr<duckdb_re2::RE2> regex;

  duckdb_re2::RE2 &Get(const string &pattern) {
    if (!regex || delimiter != pattern) {
      duckdb_re2::StringPiece pattern_piece(pattern);
      auto compiled = make_uniq<duckdb_re2::RE2>(pattern_piece);
      if (!compiled->ok()) {
        throw InvalidInputException(compiled->error());
      }
      delimiter = pattern;
      regex = std::move(compiled);
    }
    return *regex;
  }
};

struct TokenizeLocalState : public FunctionLocalState {
  CachedRegex regex;
};

static unique_ptr<FunctionLocalState>
InitTokenizeState(ExpressionState &, const BoundFunctionExpression &,
                  FunctionData *) {
  return make_uniq<TokenizeLocalState>();
}

// Mirrors StringSplitter::Split in DuckDB's string_split.cpp, spans included.
template <class EMIT>
static void SplitRegexWithSpans(const string &text, duckdb_re2::RE2 &regex,
                                EMIT &&emit) {
  auto base = text.data();
  auto input_data = base;
  auto input_size = UnsafeNumericCast<idx_t>(text.size());
  while (input_size > 0) {
    duckdb_re2::StringPiece match;
    if (!regex.Match(duckdb_re2::StringPiece(input_data, input_size), 0,
                     input_size, duckdb_re2::RE2::UNANCHORED, &match, 1)) {
      break;
    }
    auto match_size = UnsafeNumericCast<idx_t>(match.size());
    auto pos = UnsafeNumericCast<idx_t>(match.data() - input_data);
    if (match_size == 0 && pos == 0) {
      for (pos++; pos < input_size; pos++) {
        if (IsUtf8CharacterStart(input_data[pos])) {
          break;
        }
      }
      if (pos == input_size) {
        break;
      }
    }
    emit(UnsafeNumericCast<idx_t>(input_data - base), pos);
    input_data += pos + match_size;
    input_size -= pos + match_size;
  }
  emit(UnsafeNumericCast<idx_t>(input_data - base), input_size);
}

struct MappedToken {
  uint32_t norm_start;
  uint32_t norm_size;
  uint32_t orig_start;
  uint32_t orig_end;
};

static uint32_t MapWithinPiece(const NormalizedPiece &piece,
                               uint32_t norm_position) {
  // Inside a one-to-one run the mapping is exact; a width-changing codepoint
  // rounds to its original start.
  if (piece.norm_size == piece.orig_size) {
    return piece.orig_start + (norm_position - piece.norm_start);
  }
  return piece.orig_start;
}

static MappedToken MapSpan(const NormalizedText &normalized, TokenSpan span) {
  MappedToken token{span.norm_start, span.norm_size, 0, 0};
  if (span.norm_size == 0) {
    auto piece = span.norm_start < normalized.text.size()
                     ? PieceContaining(normalized, span.norm_start)
                     : nullptr;
    auto boundary = piece ? MapWithinPiece(*piece, span.norm_start)
                          : UnsafeNumericCast<uint32_t>(normalized.source_size);
    token.orig_start = boundary;
    token.orig_end = boundary;
    return token;
  }
  auto first = PieceContaining(normalized, span.norm_start);
  auto last = PieceContaining(normalized, span.norm_start + span.norm_size - 1);
  D_ASSERT(first && last);
  token.orig_start = MapWithinPiece(*first, span.norm_start);
  token.orig_end = last->norm_size == last->orig_size
                       ? last->orig_start + (span.norm_start + span.norm_size -
                                             last->norm_start)
                       : last->orig_start + last->orig_size;
  return token;
}

static void WriteToken(VectorWriter<string_t> &writer, string_t text,
                       const NormalizedText &, TokenSpan span) {
  writer.WriteStringRef(
      string_t(text.GetData() + span.norm_start, span.norm_size));
}

using SpanToken = VectorStructType<string_t, uint32_t, uint32_t>;

static void WriteToken(VectorWriter<SpanToken> &writer, string_t text,
                       const NormalizedText &normalized, TokenSpan span) {
  auto mapped = MapSpan(normalized, span);
  writer.WriteValue([&](VectorWriter<string_t> &term,
                        VectorWriter<uint32_t> &start,
                        VectorWriter<uint32_t> &end) {
    term.WriteStringRef(
        string_t(text.GetData() + span.norm_start, span.norm_size));
    start.WriteValue(mapped.orig_start);
    end.WriteValue(mapped.orig_end);
  });
}

} // namespace

template <bool WITH_OFFSETS>
static void TokenizeFunction(DataChunk &args, ExpressionState &state,
                             Vector &result) {
  auto inputs = args.data[0].Values<string_t>();
  auto tokenizers = args.data[1].Values<string_t>();
  auto ignores = args.data[2].Values<string_t>();
  auto strip_flags = args.data[3].Values<bool>();
  auto lower_flags = args.data[4].Values<bool>();

  result.SetVectorType(VectorType::FLAT_VECTOR);
  ListVector::SetListSize(result, 0);
  using TokenType =
      typename std::conditional<WITH_OFFSETS, SpanToken, string_t>::type;
  auto list_writer =
      FlatVector::Writer<VectorListType<TokenType>>(result, args.size());

  auto &cached_regex = ExecuteFunctionState::GetFunctionState(state)
                           ->Cast<TokenizeLocalState>()
                           .regex;
  for (idx_t i = 0; i < args.size(); i++) {
    auto input = inputs[i];
    if (!input.IsValid() || !tokenizers[i].IsValid() || !ignores[i].IsValid() ||
        !strip_flags[i].IsValid() || !lower_flags[i].IsValid()) {
      list_writer.WriteNull();
      continue;
    }
    auto input_value = input.GetValue();
    auto normalized = NormalizeWithMap<WITH_OFFSETS>(
        input_value.GetData(), input_value.GetSize(), strip_flags[i].GetValue(),
        lower_flags[i].GetValue());

    auto &child = ListVector::GetChildMutable(result);
    auto &raw_vector =
        WITH_OFFSETS ? StructVector::GetEntries(child)[0] : child;
    // One owned copy of normalized text; tokens reference its substrings.
    auto owned_text = StringVector::AddString(raw_vector, normalized.text);
    auto list = list_writer.WriteDynamicList();
    auto emit = [&](idx_t start, idx_t size) {
      TokenSpan span{UnsafeNumericCast<uint32_t>(start),
                     UnsafeNumericCast<uint32_t>(size)};
      WriteToken(list.WriteElement(), owned_text, normalized, span);
    };
    if (tokenizers[i].GetValue().GetString() == "opensearch_standard") {
      ScanOpenSearchStandardTokens(normalized.text.data(),
                                   normalized.text.size(), emit);
    } else {
      auto delimiter = "(" + ignores[i].GetValue().GetString() + ")|\\s+";
      SplitRegexWithSpans(normalized.text, cached_regex.Get(delimiter), emit);
    }
  }
}

template <bool WITH_OFFSETS> static ScalarFunction GetTokenizeFunction() {
  auto element_type =
      WITH_OFFSETS
          ? LogicalType::STRUCT({{"raw_term", LogicalType::VARCHAR},
                                 {"start_offset", LogicalType::UINTEGER},
                                 {"end_offset", LogicalType::UINTEGER}})
          : LogicalType::VARCHAR;
  ScalarFunction function(
      WITH_OFFSETS ? "fts_tokenize_spans" : "fts_tokenize",
      {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
       LogicalType::BOOLEAN, LogicalType::BOOLEAN},
      LogicalType::LIST(element_type), TokenizeFunction<WITH_OFFSETS>);
  function.SetNullHandling(FunctionNullHandling::SPECIAL_HANDLING);
  function.SetInitStateCallback(InitTokenizeState);
  return function;
}

ScalarFunction GetFTSTokenizeFunction() { return GetTokenizeFunction<false>(); }

ScalarFunction GetFTSTokenizeSpansFunction() {
  return GetTokenizeFunction<true>();
}

} // namespace duckdb
