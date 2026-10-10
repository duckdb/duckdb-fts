#include "fts_tokenize_spans.hpp"

#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include "duckdb/common/vector/vector_writer.hpp"
#include "fts_opensearch_tokenizer.hpp"
#include "re2/re2.h"
#include "utf8proc.hpp"
#include "utf8proc_wrapper.hpp"

namespace duckdb {

namespace {

// One entry per run of original bytes that normalize one-to-one, or per
// original codepoint whose normalization changes the byte width.
struct NormalizedPiece {
  uint32_t norm_start;
  uint32_t norm_size;
  uint32_t orig_start;
  uint32_t orig_size;
};

struct NormalizedText {
  string text;
  vector<NormalizedPiece> pieces;
};

static void AppendLowered(const string &piece, string &result) {
  idx_t pos = 0;
  while (pos < piece.size()) {
    int char_size = 0;
    auto codepoint = Utf8Proc::UTF8ToCodepoint(piece.data() + pos, char_size,
                                               piece.size() - pos);
    if (char_size <= 0) {
      result += piece[pos];
      pos++;
      continue;
    }
    auto lowered = Utf8Proc::CodepointToLower(codepoint);
    char encoded[4];
    int encoded_size = 0;
    if (Utf8Proc::CodepointToUtf8(lowered, encoded_size, encoded)) {
      result.append(encoded, UnsafeNumericCast<idx_t>(encoded_size));
    } else {
      result.append(piece.data() + pos, UnsafeNumericCast<idx_t>(char_size));
    }
    pos += UnsafeNumericCast<idx_t>(char_size);
  }
}

// A codepoint that attaches to the one before it: combining marks and the
// vowel and trailing-consonant Hangul jamo, which only compose with a leading
// jamo in front of them.
static bool IsClusterContinuation(int32_t codepoint) {
  if ((codepoint >= 0x1160 && codepoint <= 0x11FF) ||
      (codepoint >= 0xD7B0 && codepoint <= 0xD7FF)) {
    return true;
  }
  auto category = utf8proc_category(codepoint);
  return category == UTF8PROC_CATEGORY_MN || category == UTF8PROC_CATEGORY_MC ||
         category == UTF8PROC_CATEGORY_ME;
}

// Normalizes cluster by cluster with the same utf8proc calls the SQL builtins
// use, recording which original bytes produced which normalized ones. A
// cluster is a codepoint plus the marks and jamo that follow it, so a stripped
// accent stays inside the original span of its base letter and decomposed
// Hangul composes the way it does in the whole-string call.
static NormalizedText NormalizeWithMap(const char *data, idx_t size,
                                       bool strip_accents, bool lower) {
  NormalizedText result;
  result.text.reserve(size);
  idx_t pos = 0;
  while (pos < size) {
    auto lead = data[pos];
    int char_size = 1;
    int32_t codepoint = -1;
    if (!(lead & 0x80)) {
      codepoint = lead;
    } else {
      codepoint = Utf8Proc::UTF8ToCodepoint(data + pos, char_size, size - pos);
      if (char_size <= 0) {
        char_size = 1;
        codepoint = -1;
      }
    }
    auto cluster_end = pos + UnsafeNumericCast<idx_t>(char_size);
    while (codepoint >= 0 && cluster_end < size) {
      int next_size = 0;
      auto next = Utf8Proc::UTF8ToCodepoint(data + cluster_end, next_size,
                                            size - cluster_end);
      if (next_size <= 0 || !IsClusterContinuation(next)) {
        break;
      }
      cluster_end += UnsafeNumericCast<idx_t>(next_size);
    }
    if (!(lead & 0x80) && cluster_end == pos + 1) {
      // Lone ASCII: accent stripping is a no-op and lowercasing is one branch,
      // both one-to-one, so consecutive bytes extend one identity run.
      auto c = lower && lead >= 'A' && lead <= 'Z'
                   ? UnsafeNumericCast<char>(lead + 32)
                   : lead;
      if (!result.pieces.empty()) {
        auto &run = result.pieces.back();
        if (run.norm_size == run.orig_size &&
            run.norm_start + run.norm_size == result.text.size() &&
            run.orig_start + run.orig_size == pos) {
          run.norm_size++;
          run.orig_size++;
          result.text += c;
          pos++;
          continue;
        }
      }
      result.pieces.push_back({UnsafeNumericCast<uint32_t>(result.text.size()),
                               1, UnsafeNumericCast<uint32_t>(pos), 1});
      result.text += c;
      pos++;
      continue;
    }
    auto cluster_size = cluster_end - pos;
    string piece(data + pos, cluster_size);
    if (codepoint >= 0) {
      if (strip_accents) {
        auto stripped = utf8proc_remove_accents(
            reinterpret_cast<const utf8proc_uint8_t *>(piece.c_str()),
            UnsafeNumericCast<utf8proc_ssize_t>(piece.size()));
        if (stripped) {
          piece = const_char_ptr_cast(stripped);
          free(stripped);
        }
      }
      if (lower && !piece.empty()) {
        string lowered;
        AppendLowered(piece, lowered);
        piece = std::move(lowered);
      }
    }
    if (!piece.empty()) {
      result.pieces.push_back({UnsafeNumericCast<uint32_t>(result.text.size()),
                               UnsafeNumericCast<uint32_t>(piece.size()),
                               UnsafeNumericCast<uint32_t>(pos),
                               UnsafeNumericCast<uint32_t>(cluster_size)});
      result.text += piece;
    }
    pos = cluster_end;
  }
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

// Compiled once per distinct delimiter: the pattern is constant per query.
struct CachedRegex {
  string delimiter;
  unique_ptr<duckdb_re2::RE2> regex;

  duckdb_re2::RE2 &Get(const string &pattern) {
    if (!regex || delimiter != pattern) {
      duckdb_re2::StringPiece pattern_piece(pattern);
      regex = make_uniq<duckdb_re2::RE2>(pattern_piece);
      if (!regex->ok()) {
        throw InvalidInputException(regex->error());
      }
      delimiter = pattern;
    }
    return *regex;
  }
};

// Mirrors StringSplitter::Split in DuckDB's string_split.cpp, spans included.
static void SplitRegexWithSpans(const string &text, duckdb_re2::RE2 &regex,
                                vector<TokenSpan> &spans) {
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
    spans.push_back({UnsafeNumericCast<uint32_t>(input_data - base),
                     UnsafeNumericCast<uint32_t>(pos)});
    input_data += pos + match_size;
    input_size -= pos + match_size;
  }
  spans.push_back({UnsafeNumericCast<uint32_t>(input_data - base),
                   UnsafeNumericCast<uint32_t>(input_size)});
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

static MappedToken MapSpan(const NormalizedText &normalized, idx_t orig_size,
                           TokenSpan span) {
  MappedToken token{span.norm_start, span.norm_size, 0, 0};
  if (span.norm_size == 0) {
    auto piece = span.norm_start < normalized.text.size()
                     ? PieceContaining(normalized, span.norm_start)
                     : nullptr;
    auto boundary = piece ? MapWithinPiece(*piece, span.norm_start)
                          : UnsafeNumericCast<uint32_t>(orig_size);
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

} // namespace

static void TokenizeSpansFunction(DataChunk &args, ExpressionState &state,
                                  Vector &result) {
  auto inputs = args.data[0].Values<string_t>();
  auto tokenizers = args.data[1].Values<string_t>();
  auto ignores = args.data[2].Values<string_t>();
  auto strip_flags = args.data[3].Values<bool>();
  auto lower_flags = args.data[4].Values<bool>();

  result.SetVectorType(VectorType::FLAT_VECTOR);
  auto list_entries = FlatVector::GetDataMutable<list_entry_t>(result);
  auto &list_validity = FlatVector::ValidityMutable(result);

  CachedRegex cached_regex;
  idx_t total = 0;
  for (idx_t i = 0; i < args.size(); i++) {
    auto input = inputs[i];
    if (!input.IsValid() || !tokenizers[i].IsValid() || !ignores[i].IsValid() ||
        !strip_flags[i].IsValid() || !lower_flags[i].IsValid()) {
      list_validity.SetInvalid(i);
      list_entries[i].offset = 0;
      list_entries[i].length = 0;
      continue;
    }
    auto input_value = input.GetValue();
    auto normalized =
        NormalizeWithMap(input_value.GetData(), input_value.GetSize(),
                         strip_flags[i].GetValue(), lower_flags[i].GetValue());

    vector<TokenSpan> spans;
    if (tokenizers[i].GetValue().GetString() == "opensearch_standard") {
      ScanOpenSearchStandardTokens(
          normalized.text.data(), normalized.text.size(),
          [&](idx_t start, idx_t size) {
            spans.push_back({UnsafeNumericCast<uint32_t>(start),
                             UnsafeNumericCast<uint32_t>(size)});
          });
    } else {
      auto delimiter = "(" + ignores[i].GetValue().GetString() + ")|\\s+";
      SplitRegexWithSpans(normalized.text, cached_regex.Get(delimiter), spans);
    }

    list_entries[i].offset = total;
    list_entries[i].length = spans.size();
    ListVector::Reserve(result, total + spans.size());
    auto &child = ListVector::GetEntry(result);
    auto &members = StructVector::GetEntries(child);
    auto raw_terms = FlatVector::GetDataMutable<string_t>(members[0]);
    auto start_offsets = FlatVector::GetDataMutable<uint32_t>(members[1]);
    auto end_offsets = FlatVector::GetDataMutable<uint32_t>(members[2]);
    for (auto &span : spans) {
      auto mapped = MapSpan(normalized, input_value.GetSize(), span);
      raw_terms[total] = StringVector::AddString(
          members[0], normalized.text.data() + mapped.norm_start,
          mapped.norm_size);
      start_offsets[total] = mapped.orig_start;
      end_offsets[total] = mapped.orig_end;
      total++;
    }
  }
  ListVector::SetListSize(result, total);
}

ScalarFunction GetFTSTokenizeSpansFunction() {
  auto span_type = LogicalType::STRUCT({{"raw_term", LogicalType::VARCHAR},
                                        {"start_offset", LogicalType::UINTEGER},
                                        {"end_offset", LogicalType::UINTEGER}});
  ScalarFunction function("fts_tokenize_spans",
                          {LogicalType::VARCHAR, LogicalType::VARCHAR,
                           LogicalType::VARCHAR, LogicalType::BOOLEAN,
                           LogicalType::BOOLEAN},
                          LogicalType::LIST(span_type), TokenizeSpansFunction);
  function.SetNullHandling(FunctionNullHandling::SPECIAL_HANDLING);
  return function;
}

} // namespace duckdb
