#include "canvas/poc03/large_scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/heap.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace canvas::poc03 {
namespace {

struct Options {
  uint32_t nodes = 100000;
  uint32_t frames = 600;
  uint32_t updates = 1000;
  uint64_t seed = 0x43414e5641533033ULL;
  uint32_t smoke_seconds = 0;
  std::string output;
};

uint64_t ParseUnsigned(std::string_view value, std::string_view name) {
  if (value.empty()) {
    throw std::invalid_argument(std::string(name) + " must not be empty");
  }
  size_t parsed = 0;
  const uint64_t result = std::stoull(std::string(value), &parsed, 0);
  if (parsed != value.size()) {
    throw std::invalid_argument(std::string(name) + " is not an integer");
  }
  return result;
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto split = argument.find('=');
    const std::string_view key = argument.substr(0, split);
    const std::string_view value = split == std::string_view::npos
                                       ? std::string_view{}
                                       : argument.substr(split + 1U);
    if (key == "--nodes") {
      options.nodes = static_cast<uint32_t>(ParseUnsigned(value, key));
    } else if (key == "--frames") {
      options.frames = static_cast<uint32_t>(ParseUnsigned(value, key));
    } else if (key == "--updates") {
      options.updates = static_cast<uint32_t>(ParseUnsigned(value, key));
    } else if (key == "--seed") {
      options.seed = ParseUnsigned(value, key);
    } else if (key == "--smoke") {
      options.smoke_seconds = static_cast<uint32_t>(ParseUnsigned(value, key));
    } else if (key == "--output") {
      options.output = std::string(value);
    } else {
      throw std::invalid_argument("unknown argument: " + std::string(argument));
    }
  }
  if (options.nodes == 0 || options.frames == 0) {
    throw std::invalid_argument("nodes and frames must be positive");
  }
  return options;
}

double Percentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>(
      std::ceil(percentile * static_cast<double>(values.size())) - 1.0);
  return values[std::min(index, values.size() - 1U)];
}

double Mebibytes(size_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

size_t ProcessPeakBytes() {
#if defined(__EMSCRIPTEN__)
  return emscripten_get_heap_size();
#elif defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  return GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                              sizeof(counters))
             ? static_cast<size_t>(counters.PeakWorkingSetSize)
             : 0U;
#else
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0U;
  }
#if defined(__APPLE__)
  return static_cast<size_t>(usage.ru_maxrss);
#else
  return static_cast<size_t>(usage.ru_maxrss) * 1024U;
#endif
#endif
}

std::string JsonEscape(std::string_view value) {
  std::string result;
  for (const char character : value) {
    if (character == '\\' || character == '"') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  return result;
}

int Run(const Options& options) {
  Document document = GenerateDocument(
      GeneratorConfig{options.nodes, options.seed, 1000U, 32.0F});
  SceneCompiler compiler;
  CompileDiagnostics full_diagnostics;
  const auto full_start = std::chrono::steady_clock::now();
  RuntimeScene scene = compiler.CompileFull(document, &full_diagnostics);
  const auto full_end = std::chrono::steady_clock::now();

  std::vector<double> update_ms;
  update_ms.reserve(options.updates);
  size_t maximum_records_touched = 0;
  size_t full_fallbacks = 0;
  std::string error;
  for (uint32_t update = 0; update < options.updates; ++update) {
    const uint64_t id = 1U + (static_cast<uint64_t>(update) * 7919U) %
                                options.nodes;
    NodeRecord changed = *document.Find(id);
    changed.rgba ^= 0x00010101U;
    ++changed.content_revision;
    ChangeSet changes;
    if (!document.Apply(Operation{OperationKind::kUpdate, id, changed},
                        &changes, &error)) {
      throw std::runtime_error(error);
    }
    const auto start = std::chrono::steady_clock::now();
    CompileDiagnostics diagnostics;
    if (!compiler.ApplyIncremental(document, changes, &scene, &diagnostics,
                                   &error)) {
      throw std::runtime_error(error);
    }
    const auto end = std::chrono::steady_clock::now();
    update_ms.push_back(std::chrono::duration<double, std::milli>(end - start)
                            .count());
    maximum_records_touched = std::max(maximum_records_touched,
                                       diagnostics.records_touched);
    full_fallbacks += diagnostics.full_fallbacks;
  }

  std::vector<double> frame_ms;
  frame_ms.reserve(options.frames);
  size_t maximum_candidates = 0;
  size_t maximum_visible = 0;
  TileCache cache(64U * 1024U * 1024U);
  DeterministicFrameScheduler scheduler;
  const auto smoke_deadline = std::chrono::steady_clock::now() +
      std::chrono::seconds(options.smoke_seconds);
  uint64_t frame_number = 0;
  do {
    const float pan_x = static_cast<float>((frame_number * 37U) % 28000U);
    const float pan_y = static_cast<float>((frame_number * 17U) % 2200U);
    const float zoom = 0.75F + static_cast<float>(frame_number % 8U) * 0.125F;
    const ViewState view{1U, frame_number + 1U, 1U,
                         Bounds{pan_x, pan_y, pan_x + 1920.0F / zoom,
                                pan_y + 1080.0F / zoom},
                         zoom, 1.0F, 1920U, 1080U};
    scheduler.Invalidate(FrameInvalidation{
        view.view_id, scene.source_revision(), view.view_revision, 0U,
        view.target_generation,
        static_cast<uint32_t>(InvalidationReason::kView)});
    const auto start = std::chrono::steady_clock::now();
    const ViewQueryResult query = QueryView(scene, view, std::nullopt);
    FrameGraph graph = BuildFrame(scene, query, {});
    const std::string before_optimization = graph.VisualDigest();
    OptimizeFrameGraph(&graph);
    if (graph.VisualDigest() != before_optimization) {
      throw std::logic_error("physical pass optimization changed visual digest");
    }
    const auto frame = scheduler.Pump(view.view_id, view.target_generation);
    if (!frame || !scheduler.Present(*frame, view.target_generation)) {
      throw std::logic_error("deterministic frame scheduling failed");
    }
    const auto end = std::chrono::steady_clock::now();
    frame_ms.push_back(std::chrono::duration<double, std::milli>(end - start)
                           .count());
    maximum_candidates = std::max(maximum_candidates, query.candidates.size());
    maximum_visible = std::max(maximum_visible, query.visible.size());
    const TileKey key{view.view_id, scene.source_revision(), 1U, 1U,
                      query.scale_bucket, 1U,
                      static_cast<int32_t>(frame_number % 64U), 0};
    if (!cache.Find(key)) {
      cache.Put(key, 256U * 256U * 4U);
    }
    ++frame_number;
  } while (frame_number < options.frames ||
           (options.smoke_seconds > 0 &&
            std::chrono::steady_clock::now() < smoke_deadline));

  const RuntimeScene oracle = compiler.CompileFull(document);
  const bool equivalent = scene.Digest() == oracle.Digest() &&
                          scene.ContentBounds() == oracle.ContentBounds();
  const double full_ms =
      std::chrono::duration<double, std::milli>(full_end - full_start).count();
  const double p50 = Percentile(frame_ms, 0.50);
  const double p95 = Percentile(frame_ms, 0.95);
  const double p99 = Percentile(frame_ms, 0.99);
  const double maximum = *std::max_element(frame_ms.begin(), frame_ms.end());
  const size_t estimated_bytes = document.EstimatedBytes() +
                                 scene.EstimatedBytes() + cache.stats().bytes;

  std::ostringstream json;
  json << std::fixed << std::setprecision(3)
       << "{\n"
       << "  \"schema_version\": 1,\n"
       << "  \"poc\": \"POC-03 100K Scene\",\n"
       << "  \"seed\": " << options.seed << ",\n"
       << "  \"nodes\": " << options.nodes << ",\n"
       << "  \"document_revision\": " << document.revision() << ",\n"
       << "  \"document_digest\": \"" << JsonEscape(document.Digest()) << "\",\n"
       << "  \"scene_digest\": \"" << JsonEscape(scene.Digest()) << "\",\n"
       << "  \"full_incremental_equivalent\": "
       << (equivalent ? "true" : "false") << ",\n"
       << "  \"full_compile_ms\": " << full_ms << ",\n"
       << "  \"incremental_p95_ms\": " << Percentile(update_ms, 0.95) << ",\n"
       << "  \"maximum_records_touched\": " << maximum_records_touched << ",\n"
       << "  \"full_fallbacks\": " << full_fallbacks << ",\n"
       << "  \"frames\": " << frame_ms.size() << ",\n"
       << "  \"frame_p50_ms\": " << p50 << ",\n"
       << "  \"frame_p95_ms\": " << p95 << ",\n"
       << "  \"frame_p99_ms\": " << p99 << ",\n"
       << "  \"frame_max_ms\": " << maximum << ",\n"
       << "  \"maximum_candidates\": " << maximum_candidates << ",\n"
       << "  \"maximum_visible\": " << maximum_visible << ",\n"
       << "  \"estimated_runtime_mib\": " << Mebibytes(estimated_bytes) << ",\n"
       << "  \"process_peak_mib\": " << Mebibytes(ProcessPeakBytes()) << ",\n"
       << "  \"cache_bytes\": " << cache.stats().bytes << ",\n"
       << "  \"cache_hits\": " << cache.stats().hits << ",\n"
       << "  \"cache_misses\": " << cache.stats().misses << ",\n"
       << "  \"refresh_rate_hz\": 0,\n"
       << "  \"missed_presentations\": 0,\n"
       << "  \"timing_source\": \"headless-steady-clock-not-vsync\"\n"
       << "}\n";
  if (!options.output.empty()) {
    std::ofstream output(options.output);
    if (!output) {
      throw std::runtime_error("could not open output file");
    }
    output << json.str();
  }
  std::cout << json.str();
  return equivalent && maximum_candidates <= 5000U &&
                 maximum_records_touched <= 1U
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}

}  // namespace
}  // namespace canvas::poc03

int main(int argc, char** argv) {
  try {
    return canvas::poc03::Run(canvas::poc03::ParseOptions(argc, argv));
  } catch (const std::exception& exception) {
    std::cerr << "canvas_poc03_benchmark: " << exception.what() << '\n';
    return EXIT_FAILURE;
  }
}
