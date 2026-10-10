#include "fts_highlight.hpp"

#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#include "duckdb/common/vector/vector_writer.hpp"

#include <algorithm>

namespace duckdb {

namespace {

struct HighlightSpan {
  uint32_t start;
  uint32_t end;
};

// Overlapping or touching spans render as one, the way FTS5 merges a phrase
// with a term inside it or with an overlapping phrase.
static void MergeSpans(vector<HighlightSpan> &spans) {
  std::sort(spans.begin(), spans.end(),
            [](const HighlightSpan &a, const HighlightSpan &b) {
              return a.start < b.start || (a.start == b.start && a.end < b.end);
            });
  idx_t merged = 0;
  for (idx_t i = 0; i < spans.size(); i++) {
    if (merged > 0 && spans[i].start <= spans[merged - 1].end) {
      spans[merged - 1].end = MaxValue(spans[merged - 1].end, spans[i].end);
    } else {
      spans[merged++] = spans[i];
    }
  }
  spans.resize(merged);
}

} // namespace

static void RenderHighlightFunction(DataChunk &args, ExpressionState &state,
                                    Vector &result) {
  auto texts = args.data[0].Values<string_t>();
  auto start_lists = args.data[1].Values<VectorListType<uint32_t>>();
  auto end_lists = args.data[2].Values<VectorListType<uint32_t>>();
  auto befores = args.data[3].Values<string_t>();
  auto afters = args.data[4].Values<string_t>();
  auto window_froms = args.data[5].Values<uint32_t>();
  auto window_tos = args.data[6].Values<uint32_t>();
  auto leadings = args.data[7].Values<bool>();
  auto trailings = args.data[8].Values<bool>();
  auto ellipses = args.data[9].Values<string_t>();

  auto writer = FlatVector::Writer<string_t>(result, args.size());
  for (idx_t i = 0; i < args.size(); i++) {
    if (!texts[i].IsValid() || !start_lists[i].IsValid() ||
        !end_lists[i].IsValid() || !befores[i].IsValid() ||
        !afters[i].IsValid() || !window_froms[i].IsValid() ||
        !window_tos[i].IsValid() || !leadings[i].IsValid() ||
        !trailings[i].IsValid() || !ellipses[i].IsValid()) {
      writer.WriteNull();
      continue;
    }
    auto text = texts[i].GetValue();
    auto window_from = window_froms[i].GetValue();
    auto window_to = MinValue(window_tos[i].GetValue(),
                              UnsafeNumericCast<uint32_t>(text.GetSize()));
    window_from = MinValue(window_from, window_to);

    vector<uint32_t> span_starts;
    vector<uint32_t> span_ends;
    span_starts.reserve(start_lists[i].GetListLength());
    span_ends.reserve(end_lists[i].GetListLength());
    for (const auto value : start_lists[i].GetChildValues()) {
      span_starts.push_back(value.IsValid() ? value.GetValue() : window_to);
    }
    for (const auto value : end_lists[i].GetChildValues()) {
      span_ends.push_back(value.IsValid() ? value.GetValue() : window_from);
    }
    vector<HighlightSpan> spans;
    auto span_count = MinValue<idx_t>(span_starts.size(), span_ends.size());
    for (idx_t j = 0; j < span_count; j++) {
      auto span_start = MaxValue(span_starts[j], window_from);
      auto span_end = MinValue(span_ends[j], window_to);
      if (span_start < span_end) {
        spans.push_back({span_start, span_end});
      }
    }
    MergeSpans(spans);

    auto data = text.GetData();
    string rendered;
    if (leadings[i].GetValue()) {
      rendered += ellipses[i].GetValue().GetString();
    }
    auto cursor = window_from;
    for (auto &span : spans) {
      rendered.append(data + cursor, span.start - cursor);
      rendered += befores[i].GetValue().GetString();
      rendered.append(data + span.start, span.end - span.start);
      rendered += afters[i].GetValue().GetString();
      cursor = span.end;
    }
    rendered.append(data + cursor, window_to - cursor);
    if (trailings[i].GetValue()) {
      rendered += ellipses[i].GetValue().GetString();
    }
    writer.WriteValue(rendered);
  }
}

ScalarFunction GetFTSRenderHighlightFunction() {
  ScalarFunction function(
      "fts_render_highlight",
      {LogicalType::VARCHAR, LogicalType::LIST(LogicalType::UINTEGER),
       LogicalType::LIST(LogicalType::UINTEGER), LogicalType::VARCHAR,
       LogicalType::VARCHAR, LogicalType::UINTEGER, LogicalType::UINTEGER,
       LogicalType::BOOLEAN, LogicalType::BOOLEAN, LogicalType::VARCHAR},
      LogicalType::VARCHAR, RenderHighlightFunction);
  function.SetNullHandling(FunctionNullHandling::SPECIAL_HANDLING);
  return function;
}

} // namespace duckdb
