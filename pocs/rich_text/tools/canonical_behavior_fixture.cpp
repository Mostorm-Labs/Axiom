#include "canonical_behavior_fixture.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "canvas_poc04/rich_text.h"
#include "skparagraph_layout.h"

namespace canvas::poc04 {
namespace {

struct BehaviorResults {
  bool english = false;
  bool simplified_chinese = false;
  bool pinyin_composition = false;
  bool newline = false;
  bool mixed_runs = false;
  bool selection = false;
  bool caret = false;
  bool clipboard = false;
  bool undo = false;
  bool redo = false;

  [[nodiscard]] bool AllPassed() const {
    return english && simplified_chinese && pinyin_composition && newline &&
           mixed_runs && selection && caret && clipboard && undo && redo;
  }
};

double Percentile(std::vector<double> values, double percentile) {
  std::sort(values.begin(), values.end());
  return values[static_cast<size_t>((values.size() - 1) * percentile)];
}

double Maximum(const std::vector<double>& values) {
  return *std::max_element(values.begin(), values.end());
}

double Median(const std::vector<double>& values) {
  return Percentile(values, 0.50);
}

void AppendPerformanceJson(std::ostringstream* output, std::string_view name,
                           const std::vector<double>& samples,
                           uint32_t warmup_samples) {
  *output << '"' << name << "_samples\":" << samples.size()
          << ",\"" << name << "_warmup_samples\":" << warmup_samples
          << ",\"" << name << "_p50_ms\":" << Median(samples)
          << ",\"" << name << "_p95_ms\":" << Percentile(samples, 0.95)
          << ",\"" << name << "_p99_ms\":" << Percentile(samples, 0.99)
          << ",\"" << name << "_max_ms\":" << Maximum(samples);
}

std::string GeometryJson(const TextLayoutResult& layout) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(4);
  output << "{\"height\":" << layout.height << ",\"lines\":[";
  for (size_t index = 0; index < layout.lines.size(); ++index) {
    if (index) output << ',';
    const auto& line = layout.lines[index];
    output << "{\"start\":[" << line.start.paragraph << ','
           << line.start.offset_utf16 << "],\"end\":[" << line.end.paragraph
           << ',' << line.end.offset_utf16 << "],\"baseline\":"
           << line.baseline << ",\"width\":" << line.width
           << ",\"height\":" << line.height << '}';
  }
  output << "],\"clusters\":[";
  for (size_t index = 0; index < layout.clusters.size(); ++index) {
    if (index) output << ',';
    const auto& cluster = layout.clusters[index];
    output << "{\"start\":[" << cluster.range.anchor.paragraph << ','
           << cluster.range.anchor.offset_utf16 << "],\"end\":["
           << cluster.range.focus.paragraph << ','
           << cluster.range.focus.offset_utf16 << "],\"rect\":["
           << cluster.bounds.x << ',' << cluster.bounds.y << ','
           << cluster.bounds.width << ',' << cluster.bounds.height << "]}";
  }
  output << "],\"selection\":[";
  for (size_t index = 0; index < layout.selection_rects.size(); ++index) {
    if (index) output << ',';
    const auto& rect = layout.selection_rects[index];
    output << '[' << rect.x << ',' << rect.y << ',' << rect.width << ','
           << rect.height << ']';
  }
  output << "],\"diagnostics\":[";
  for (size_t index = 0; index < layout.diagnostics.size(); ++index) {
    if (index) output << ',';
    output << '\"' << layout.diagnostics[index] << '\"';
  }
  output << "]}";
  return output.str();
}

BehaviorResults ExerciseBehavior(TextDocument* document,
                                 TextEditSession* session) {
  BehaviorResults result;
  session->Focus();
  session->InsertText(u"English\n");
  session->BeginComposition();
  session->UpdateComposition(u"中文拼音", 2, 4);
  result.caret = session->composition().has_value() &&
                 session->composition()->selection_start_utf16 == 2 &&
                 session->composition()->selection_end_utf16 == 4;
  session->CommitComposition();
  result.pinyin_composition = session->operation_log().size() == 2 &&
                              !session->composition().has_value();
  TextStyle mixed;
  mixed.rgba = 0x2563ebffU;
  session->InsertText(u" mixed runs", mixed);
  session->SetSelection({{0, 0}, {0, 7}});
  result.selection = session->selection().range() ==
                     TextRange{{0, 0}, {0, 7}};
  result.clipboard = session->CopySelection() == u"English";
  session->SetSelection({{1, 4}, {1, 4}});
  session->InsertText(u"!");
  result.undo = session->Undo();
  result.redo = session->Redo();

  const std::u16string text = document->PlainText();
  result.english = text.starts_with(u"English");
  result.simplified_chinese = text.find(u"中文") != std::u16string::npos;
  result.newline = text.find(u'\n') != std::u16string::npos;
  result.mixed_runs = document->paragraphs().size() == 2 &&
                      document->paragraphs()[1].runs.size() >= 2;
  return result;
}

std::string BehaviorJson(const BehaviorResults& result) {
  auto value = [](bool passed) { return passed ? "true" : "false"; };
  std::ostringstream output;
  output << "{\"english\":" << value(result.english)
         << ",\"simplified_chinese\":" << value(result.simplified_chinese)
         << ",\"pinyin_composition\":" << value(result.pinyin_composition)
         << ",\"newline\":" << value(result.newline)
         << ",\"mixed_runs\":" << value(result.mixed_runs)
         << ",\"selection\":" << value(result.selection)
         << ",\"caret\":" << value(result.caret)
         << ",\"clipboard\":" << value(result.clipboard)
         << ",\"undo\":" << value(result.undo)
         << ",\"redo\":" << value(result.redo) << '}';
  return output.str();
}

uint32_t ExerciseLifecycle() {
  uint32_t failures = 0;
  for (uint32_t cycle = 0; cycle < 100; ++cycle) {
    try {
      auto document = std::make_shared<TextDocument>();
      TextEditSession session(document);
      session.Focus();
      session.BeginComposition();
      session.UpdateComposition(u"discarded", 9, 9);
      session.Blur();
      if (session.focused() || session.composition().has_value() ||
          !document->PlainText().empty() || !session.operation_log().empty()) {
        ++failures;
      }
    } catch (...) {
      ++failures;
    }
  }
  return failures;
}

}  // namespace

CanonicalBehaviorArtifact BuildCanonicalBehaviorArtifact(
    std::string_view platform, std::string latin_font_path,
    std::string cjk_font_path) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  const BehaviorResults behavior = ExerciseBehavior(document.get(), &session);
  SkParagraphTextLayout layout(std::move(latin_font_path),
                               std::move(cjk_font_path));

  // Skia's pinned CJK fixture is intentionally a one-glyph subset containing
  // U+662F. Keep the full Chinese/pinyin edit corpus above, but constrain the
  // cross-platform shaping oracle to glyphs that the locked font actually
  // contains so unresolved-glyph diagnostics remain meaningful.
  auto layout_document = std::make_shared<TextDocument>();
  TextEditSession layout_session(layout_document);
  layout_session.Focus();
  layout_session.InsertText(u"English\n是 mixed runs");
  const TextLayoutResult fixture =
      layout.Layout(*layout_document, 320.0F, {{0, 0}, {0, 7}});
  const uint32_t lifecycle_failures = ExerciseLifecycle();

  auto performance_document = std::make_shared<TextDocument>();
  TextEditSession performance(performance_document);
  performance.Focus();
  performance.InsertText(std::u16string(10000, u'a'));
  constexpr uint32_t kInputWarmupSamples = 20;
  constexpr uint32_t kInputMeasuredSamples = 120;
  constexpr uint32_t kLayoutWarmupSamples = 5;
  constexpr uint32_t kLayoutMeasuredSamples = 30;
  std::vector<double> input_times;
  input_times.reserve(kInputMeasuredSamples);
  for (uint32_t iteration = 0; iteration < kInputWarmupSamples; ++iteration) {
    performance.SetSelection({{0, iteration}, {0, iteration}});
    performance.InsertText(u"x");
  }
  for (uint32_t iteration = kInputWarmupSamples;
       iteration < kInputWarmupSamples + kInputMeasuredSamples; ++iteration) {
    const auto start = std::chrono::steady_clock::now();
    performance.SetSelection({{0, iteration}, {0, iteration}});
    performance.InsertText(u"x");
    const auto end = std::chrono::steady_clock::now();
    input_times.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
  }
  std::vector<double> layout_times;
  layout_times.reserve(kLayoutMeasuredSamples);
  for (uint32_t iteration = 0; iteration < kLayoutWarmupSamples; ++iteration) {
    static_cast<void>(layout.Layout(*performance_document, 800.0F,
                                    {{0, 0}, {0, 0}}));
  }
  for (uint32_t iteration = 0; iteration < kLayoutMeasuredSamples; ++iteration) {
    const auto start = std::chrono::steady_clock::now();
    static_cast<void>(layout.Layout(*performance_document, 800.0F,
                                    {{0, 0}, {0, 0}}));
    const auto end = std::chrono::steady_clock::now();
    layout_times.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
  }
  std::ostringstream json;
  json << std::fixed << std::setprecision(6)
       << "{\"platform\":\"" << platform << "\",\"digest\":\""
       << document->Digest() << "\",\"behavior\":" << BehaviorJson(behavior)
       << ",\"layout\":" << GeometryJson(fixture)
       << ",\"lifecycle\":{\"cycles\":100,\"failures\":"
       << lifecycle_failures << "},\"performance\":{";
  AppendPerformanceJson(&json, "input_caret", input_times,
                        kInputWarmupSamples);
  json << ',';
  AppendPerformanceJson(&json, "full_layout", layout_times,
                        kLayoutWarmupSamples);
  json << "}}\n";
  const bool layout_passed = fixture.diagnostics.empty() &&
                             !fixture.lines.empty() &&
                             !fixture.clusters.empty() &&
                             !fixture.selection_rects.empty();
  const bool performance_passed =
      Percentile(input_times, 0.95) <= 16.7 &&
      Percentile(layout_times, 0.95) <= 33.3;
  return {json.str(), behavior.AllPassed() && lifecycle_failures == 0 &&
                          layout_passed && performance_passed};
}

}  // namespace canvas::poc04
