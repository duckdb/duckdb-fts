#include "fts_extension.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#include "duckdb/common/vector/vector_writer.hpp"
#include "duckdb/function/pragma_function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "fts_indexing.hpp"
#include "libstemmer.h"
#include "unicode/uchar.h"
#include "unicode/uscript.h"
#include "utf8proc_wrapper.hpp"

namespace duckdb {

static bool IsOpenSearchStandardSingleTokenScript(UScriptCode script) {
  return script == USCRIPT_HAN || script == USCRIPT_HIRAGANA;
}

static bool IsOpenSearchStandardEmoji(UChar32 codepoint) {
  return u_hasBinaryProperty(codepoint, UCHAR_EMOJI_PRESENTATION) ||
         u_hasBinaryProperty(codepoint, UCHAR_EXTENDED_PICTOGRAPHIC);
}

static bool IsOpenSearchStandardCombiningMark(UChar32 codepoint) {
  auto char_type = u_charType(codepoint);
  return char_type == U_NON_SPACING_MARK ||
         char_type == U_COMBINING_SPACING_MARK || char_type == U_ENCLOSING_MARK;
}

static bool IsOpenSearchStandardIntraTokenPunctuation(UChar32 codepoint) {
  return codepoint == '\'' || codepoint == 0x2019 || codepoint == '.' ||
         codepoint == '_';
}

static bool IsJapaneseProlongedSoundMark(UChar32 codepoint) {
  return codepoint == 0x30FC || codepoint == 0xFF70;
}

static bool IsOpenSearchStandardTokenChar(UChar32 codepoint,
                                          UScriptCode script) {
  if (u_isUWhiteSpace(codepoint) || u_ispunct(codepoint)) {
    return false;
  }
  return u_isalnum(codepoint) ||
         u_hasBinaryProperty(codepoint, UCHAR_ALPHABETIC) ||
         script == USCRIPT_KATAKANA || IsJapaneseProlongedSoundMark(codepoint);
}

static bool IsOpenSearchStandardContinuationChar(UChar32 codepoint,
                                                 UScriptCode script) {
  return !IsOpenSearchStandardSingleTokenScript(script) &&
         !IsOpenSearchStandardEmoji(codepoint) &&
         (IsOpenSearchStandardTokenChar(codepoint, script) ||
          IsOpenSearchStandardCombiningMark(codepoint));
}

template <class LIST_WRITER>
static void TokenizeOpenSearchStandard(string_t input, LIST_WRITER &list) {
  auto input_data = input.GetData();
  auto input_size = input.GetSize();
  idx_t token_start = 0;
  idx_t token_size = 0;

  auto flush_token = [&]() {
    if (token_size == 0) {
      return;
    }
    list.WriteElement().WriteStringRef(string_t(
        input_data + token_start, UnsafeNumericCast<uint32_t>(token_size)));
    token_size = 0;
  };

  auto decode_codepoint = [&](idx_t pos, UChar32 &codepoint, int &char_size,
                              UScriptCode &script) -> bool {
    if (pos >= input_size) {
      return false;
    }
    char_size = 0;
    codepoint = Utf8Proc::UTF8ToCodepoint(input_data + pos, char_size,
                                          input_size - pos);
    if (char_size <= 0) {
      return false;
    }
    UErrorCode status = U_ZERO_ERROR;
    script = uscript_getScript(codepoint, &status);
    return U_SUCCESS(status);
  };

  for (idx_t pos = 0; pos < input_size;) {
    int char_size = 0;
    UChar32 codepoint = 0;
    UScriptCode script = USCRIPT_UNKNOWN;
    if (!decode_codepoint(pos, codepoint, char_size, script)) {
      flush_token();
      pos++;
      continue;
    }

    // Proper OpenSearch parity should delegate word boundary detection to
    // Lucene's StandardTokenizer or ICU UBRK_WORD. DuckDB's bundled ICU build
    // currently disables break iteration, so this scanner keeps the known
    // OpenSearch-compatible cases local. Combining marks are continuations to
    // preserve decomposed accents such as "cafe\u0301" rather than splitting or
    // dropping the mark.
    if (IsOpenSearchStandardCombiningMark(codepoint) && token_size > 0) {
      token_size += UnsafeNumericCast<idx_t>(char_size);
    } else if (IsOpenSearchStandardSingleTokenScript(script) ||
               IsOpenSearchStandardEmoji(codepoint)) {
      flush_token();
      list.WriteElement().WriteStringRef(
          string_t(input_data + pos, UnsafeNumericCast<uint32_t>(char_size)));
    } else if (IsOpenSearchStandardIntraTokenPunctuation(codepoint) &&
               token_size > 0) {
      UChar32 next_codepoint = 0;
      int next_char_size = 0;
      UScriptCode next_script = USCRIPT_UNKNOWN;
      auto next_pos = pos + UnsafeNumericCast<idx_t>(char_size);
      if (decode_codepoint(next_pos, next_codepoint, next_char_size,
                           next_script) &&
          IsOpenSearchStandardContinuationChar(next_codepoint, next_script)) {
        token_size += UnsafeNumericCast<idx_t>(char_size);
      } else {
        flush_token();
      }
    } else if (IsOpenSearchStandardTokenChar(codepoint, script)) {
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

static void OpenSearchStandardTokenizeFunction(DataChunk &args,
                                               ExpressionState &state,
                                               Vector &result) {
  auto input_entries = args.data[0].Values<string_t>();

  D_ASSERT(result.GetType().id() == LogicalTypeId::LIST);
  result.SetVectorType(VectorType::FLAT_VECTOR);

  auto list_writer =
      FlatVector::Writer<VectorListType<string_t>>(result, args.size());
  for (idx_t i = 0; i < args.size(); i++) {
    auto input_entry = input_entries[i];
    if (!input_entry.IsValid()) {
      list_writer.WriteNull();
      continue;
    }
    auto list = list_writer.WriteDynamicList();
    TokenizeOpenSearchStandard(input_entry.GetValue(), list);
  }

  StringVector::AddHeapReference(ListVector::GetChildMutable(result),
                                 args.data[0]);
}

static void StemFunction(DataChunk &args, ExpressionState &state,
                         Vector &result) {
  auto &input_vector = args.data[0];
  auto &stemmer_vector = args.data[1];

  BinaryExecutor::Execute<string_t, string_t, string_t>(
      input_vector, stemmer_vector, result, args.size(),
      [&](string_t input, string_t stemmer) {
        auto input_data = input.GetData();
        auto input_size = input.GetSize();

        if (stemmer.GetString() == "none") {
          auto output = StringVector::AddString(result, input_data, input_size);
          return output;
        }

        struct sb_stemmer *s =
            sb_stemmer_new(stemmer.GetString().c_str(), "UTF_8");
        if (s == 0) {
          const char **stemmers = sb_stemmer_list();
          size_t n_stemmers = 0;
          while (stemmers[n_stemmers] != nullptr) {
            n_stemmers++;
          }
          throw InvalidInputException(
              "Unrecognized stemmer '%s'. Supported stemmers are: ['%s'], or "
              "use 'none' for no stemming",
              stemmer.GetString(),
              StringUtil::Join(stemmers, n_stemmers, "', '",
                               [](const char *st) { return st; }));
        }

        auto output_data = const_char_ptr_cast(sb_stemmer_stem(
            s, reinterpret_cast<const sb_symbol *>(input_data), input_size));
        auto output_size = sb_stemmer_length(s);
        auto output = StringVector::AddString(result, output_data, output_size);

        sb_stemmer_delete(s);
        return output;
      });
}

static void LoadInternal(ExtensionLoader &loader) {

  ScalarFunction stem_func("stem", {LogicalType::VARCHAR, LogicalType::VARCHAR},
                           LogicalType::VARCHAR, StemFunction);
  ScalarFunction opensearch_standard_tokenize_func(
      "fts_tokenize_opensearch_standard", {LogicalType::VARCHAR},
      LogicalType::LIST(LogicalType::VARCHAR),
      OpenSearchStandardTokenizeFunction);

  auto create_fts_index_func = PragmaFunction::PragmaCall(
      "create_fts_index", FTSIndexing::CreateFTSIndexQuery,
      {LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR);
  create_fts_index_func.named_parameters["stemmer"] = LogicalType::VARCHAR;
  create_fts_index_func.named_parameters["tokenizer"] = LogicalType::VARCHAR;
  create_fts_index_func.named_parameters["stopwords"] = LogicalType::VARCHAR;
  create_fts_index_func.named_parameters["ignore"] = LogicalType::VARCHAR;
  create_fts_index_func.named_parameters["strip_accents"] =
      LogicalType::BOOLEAN;
  create_fts_index_func.named_parameters["lower"] = LogicalType::BOOLEAN;
  create_fts_index_func.named_parameters["overwrite"] = LogicalType::BOOLEAN;
  create_fts_index_func.named_parameters["incremental"] = LogicalType::BOOLEAN;
  create_fts_index_func.named_parameters["cluster_terms"] =
      LogicalType::BOOLEAN;
  create_fts_index_func.named_parameters["layered_search"] =
      LogicalType::BOOLEAN;

  auto drop_fts_index_func = PragmaFunction::PragmaCall(
      "drop_fts_index", FTSIndexing::DropFTSIndexQuery, {LogicalType::VARCHAR});
  auto create_fts_boolean_query_macros_func = PragmaFunction::PragmaCall(
      "create_fts_boolean_query_macros",
      FTSIndexing::CreateFTSBooleanQueryMacrosQuery, {LogicalType::VARCHAR});

  loader.RegisterFunction(stem_func);
  loader.RegisterFunction(opensearch_standard_tokenize_func);
  loader.RegisterFunction(create_fts_index_func);
  loader.RegisterFunction(create_fts_boolean_query_macros_func);
  loader.RegisterFunction(drop_fts_index_func);
}

void FtsExtension::Load(ExtensionLoader &loader) { LoadInternal(loader); }

std::string FtsExtension::Name() { return "fts"; }

std::string FtsExtension::Version() const {
#ifdef EXT_VERSION_FTS
  return EXT_VERSION_FTS;
#else
  return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(fts, loader) { duckdb::LoadInternal(loader); }
}
