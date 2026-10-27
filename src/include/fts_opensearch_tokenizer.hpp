#pragma once

#include "fts_unicode_classifier.hpp"
#include "utf8proc_wrapper.hpp"

namespace duckdb {

inline bool IsOpenSearchStandardSingleTokenScript(FTSUnicodeScript script) {
  return script == FTSUnicodeScript::HAN ||
         script == FTSUnicodeScript::HIRAGANA;
}

inline bool IsOpenSearchStandardIntraTokenPunctuation(int32_t codepoint) {
  return codepoint == '\'' || codepoint == 0x2019 || codepoint == '.' ||
         codepoint == '_';
}

inline bool IsJapaneseProlongedSoundMark(int32_t codepoint) {
  return codepoint == 0x30FC || codepoint == 0xFF70;
}

inline bool
IsOpenSearchStandardTokenChar(int32_t codepoint,
                              const FTSUnicodeProperties &properties) {
  if (properties.whitespace || properties.punctuation) {
    return false;
  }
  return properties.alphabetic || properties.decimal_number ||
         properties.script == FTSUnicodeScript::KATAKANA ||
         IsJapaneseProlongedSoundMark(codepoint);
}

inline bool
IsOpenSearchStandardContinuationChar(int32_t codepoint,
                                     const FTSUnicodeProperties &properties) {
  return !IsOpenSearchStandardSingleTokenScript(properties.script) &&
         !properties.emoji &&
         (IsOpenSearchStandardTokenChar(codepoint, properties) ||
          properties.combining_mark);
}

template <class EMIT>
void ScanOpenSearchStandardTokens(const char *input_data, idx_t input_size,
                                  EMIT &&emit) {
  idx_t token_start = 0;
  idx_t token_size = 0;

  auto flush_token = [&]() {
    if (token_size == 0) {
      return;
    }
    emit(token_start, token_size);
    token_size = 0;
  };

  auto decode_codepoint = [&](idx_t pos, int32_t &codepoint, int &char_size,
                              FTSUnicodeProperties &properties) -> bool {
    if (pos >= input_size) {
      return false;
    }
    char_size = 0;
    codepoint = Utf8Proc::UTF8ToCodepoint(input_data + pos, char_size,
                                          input_size - pos);
    if (char_size <= 0) {
      return false;
    }
    properties = FTSUnicodeClassifier::Classify(codepoint);
    return true;
  };

  for (idx_t pos = 0; pos < input_size;) {
    int char_size = 0;
    int32_t codepoint = 0;
    FTSUnicodeProperties properties{};
    if (!decode_codepoint(pos, codepoint, char_size, properties)) {
      flush_token();
      pos++;
      continue;
    }

    // Proper OpenSearch parity should delegate word boundary detection to
    // Lucene's StandardTokenizer or ICU UBRK_WORD. DuckDB's bundled ICU build
    // currently disables break iteration, so this scanner keeps the known
    // OpenSearch-compatible cases local. Combining marks are continuations to
    // preserve decomposed accents such as "café" rather than splitting or
    // dropping the mark.
    if (properties.combining_mark && token_size > 0) {
      token_size += UnsafeNumericCast<idx_t>(char_size);
    } else if (IsOpenSearchStandardSingleTokenScript(properties.script) ||
               properties.emoji) {
      flush_token();
      emit(pos, UnsafeNumericCast<idx_t>(char_size));
    } else if (IsOpenSearchStandardIntraTokenPunctuation(codepoint) &&
               token_size > 0) {
      int32_t next_codepoint = 0;
      int next_char_size = 0;
      FTSUnicodeProperties next_properties{};
      auto next_pos = pos + UnsafeNumericCast<idx_t>(char_size);
      if (decode_codepoint(next_pos, next_codepoint, next_char_size,
                           next_properties) &&
          IsOpenSearchStandardContinuationChar(next_codepoint,
                                               next_properties)) {
        token_size += UnsafeNumericCast<idx_t>(char_size);
      } else {
        flush_token();
      }
    } else if (IsOpenSearchStandardTokenChar(codepoint, properties)) {
      if (token_size == 0) {
        token_start = pos;
      }
      token_size += UnsafeNumericCast<idx_t>(char_size);
    } else {
      flush_token();
    }
    pos += UnsafeNumericCast<idx_t>(char_size);
  }
  flush_token();
}

} // namespace duckdb
