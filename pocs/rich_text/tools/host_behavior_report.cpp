#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "canvas_poc04/rich_text.h"

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
};

double Percentile(std::vector<double> values, double percentile) {
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>((values.size() - 1) * percentile);
  return values[index];
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

std::string BehaviorJson(const BehaviorResults& result) {
  auto value = [](bool passed) { return passed ? "true" : "false"; };
  return "{\"english\":" + std::string(value(result.english)) +
         ",\"simplified_chinese\":" + value(result.simplified_chinese) +
         ",\"pinyin_composition\":" + value(result.pinyin_composition) +
         ",\"newline\":" + value(result.newline) +
         ",\"mixed_runs\":" + value(result.mixed_runs) +
         ",\"selection\":" + value(result.selection) +
         ",\"caret\":" + value(result.caret) +
         ",\"clipboard\":" + value(result.clipboard) +
         ",\"undo\":" + value(result.undo) +
         ",\"redo\":" + value(result.redo) + "}";
}

uint32_t ExerciseLifecycle() {
  using namespace canvas::poc04;
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

int main(int argc, char** argv) {
  using namespace canvas::poc04;
  std::string platform = "host";
  std::string output;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument.starts_with("--platform=")) platform = argument.substr(11);
    if (argument.starts_with("--output=")) output = argument.substr(9);
  }
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"English\n");
  session.BeginComposition();
  session.UpdateComposition(u"中文拼音", 2, 4);
  BehaviorResults behavior;
  behavior.caret = session.composition().has_value() &&
                   session.composition()->selection_start_utf16 == 2 &&
                   session.composition()->selection_end_utf16 == 4;
  session.CommitComposition();
  behavior.pinyin_composition = session.operation_log().size() == 2 &&
                                !session.composition().has_value();
  TextStyle mixed;
  mixed.rgba = 0x2563ebffU;
  session.InsertText(u" mixed runs", mixed);
  session.SetSelection({{0, 0}, {0, 7}});
  behavior.selection = session.selection().range() ==
                       TextRange{{0, 0}, {0, 7}};
  behavior.clipboard = session.CopySelection() == u"English";
  session.SetSelection({{1, 4}, {1, 4}});
  session.InsertText(u"!");
  behavior.undo = session.Undo();
  behavior.redo = session.Redo();
  const std::u16string text = document->PlainText();
  behavior.english = text.starts_with(u"English");
  behavior.simplified_chinese = text.find(u"中文") != std::u16string::npos;
  behavior.newline = text.find(u'\n') != std::u16string::npos;
  behavior.mixed_runs = document->paragraphs().size() == 2 &&
                        document->paragraphs()[1].runs.size() >= 2;
  const uint32_t lifecycle_failures = ExerciseLifecycle();

  auto perf_document = std::make_shared<TextDocument>();
  TextEditSession perf(perf_document);
  perf.Focus();
  perf.InsertText(std::u16string(10000, u'a'));
  constexpr uint32_t kInputWarmupSamples = 20;
  constexpr uint32_t kInputMeasuredSamples = 120;
  constexpr uint32_t kLayoutWarmupSamples = 5;
  constexpr uint32_t kLayoutMeasuredSamples = 30;
  std::vector<double> input_times;
  input_times.reserve(kInputMeasuredSamples);
  for (uint32_t iteration = 0; iteration < kInputWarmupSamples; ++iteration) {
    perf.SetSelection({{0, iteration}, {0, iteration}});
    perf.InsertText(u"x");
  }
  for (uint32_t iteration = kInputWarmupSamples;
       iteration < kInputWarmupSamples + kInputMeasuredSamples; ++iteration) {
    const auto start = std::chrono::steady_clock::now();
    perf.SetSelection({{0, static_cast<uint32_t>(iteration)},
                       {0, static_cast<uint32_t>(iteration)}});
    perf.InsertText(u"x");
    const auto end = std::chrono::steady_clock::now();
    input_times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
  }
  DeterministicTextLayout layout;
  std::vector<double> layout_times;
  layout_times.reserve(kLayoutMeasuredSamples);
  for (uint32_t iteration = 0; iteration < kLayoutWarmupSamples; ++iteration) {
    static_cast<void>(layout.Layout(*perf_document, 800.0F, {{0, 0}, {0, 0}}));
  }
  for (uint32_t iteration = 0; iteration < kLayoutMeasuredSamples; ++iteration) {
    const auto start = std::chrono::steady_clock::now();
    static_cast<void>(layout.Layout(*perf_document, 800.0F, {{0, 0}, {0, 0}}));
    const auto end = std::chrono::steady_clock::now();
    layout_times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
  }
  std::string json =
      "{\"platform\":\"" + platform + "\",\"digest\":\"" + document->Digest() +
      "\",\"behavior\":" + BehaviorJson(behavior) +
      ",\"layout\":{\"backend\":\"deterministic-probe\",\"corpus\":\"v1\"},"
      "\"lifecycle\":{\"cycles\":100,\"failures\":" +
      std::to_string(lifecycle_failures) + "},\"performance\":{";
  std::ostringstream performance_json;
  AppendPerformanceJson(&performance_json, "input_caret", input_times,
                        kInputWarmupSamples);
  performance_json << ',';
  AppendPerformanceJson(&performance_json, "full_layout", layout_times,
                        kLayoutWarmupSamples);
  json += performance_json.str() + "}}\n";
  if (output.empty()) {
    std::cout << json;
  } else {
    std::ofstream(output) << json;
  }
  return lifecycle_failures == 0 ? 0 : 1;
}
