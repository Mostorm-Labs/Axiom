#include "canvas/poc03/ink_integration.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::poc03 {
namespace {

struct Options {
  uint32_t nodes = 100000U;
  uint64_t seed = UINT64_C(0x43414e5641533033);
  std::string output;
};

uint64_t ParseUnsigned(std::string_view value, std::string_view name) {
  size_t parsed = 0U;
  const uint64_t result = std::stoull(std::string(value), &parsed, 0);
  if (value.empty() || parsed != value.size()) {
    throw std::invalid_argument(std::string(name) + " is not an integer");
  }
  return result;
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const size_t split = argument.find('=');
    const std::string_view key = argument.substr(0U, split);
    const std::string_view value = split == std::string_view::npos
                                       ? std::string_view{}
                                       : argument.substr(split + 1U);
    if (key == "--nodes") {
      options.nodes = static_cast<uint32_t>(ParseUnsigned(value, key));
    } else if (key == "--seed") {
      options.seed = ParseUnsigned(value, key);
    } else if (key == "--output") {
      options.output = std::string(value);
    } else {
      throw std::invalid_argument("unknown argument: " + std::string(argument));
    }
  }
  if (options.nodes != 1000U && options.nodes != 10000U &&
      options.nodes != 50000U && options.nodes != 100000U) {
    throw std::invalid_argument("nodes must be one of 1000, 10000, 50000, 100000");
  }
  return options;
}

uint32_t HistoricalStrokeCount(uint32_t nodes) {
  return nodes / 5U;
}

double Mebibytes(size_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

int Run(const Options& options) {
  Document document;
  RuntimeScene scene;
  InkGeometryStore geometry;
  TileCache cache(64U * 1024U * 1024U);
  DeterministicFrameScheduler scheduler;
  IntegratedScaleReport scale;
  std::string error;
  const auto build_start = std::chrono::steady_clock::now();
  if (!BuildIntegratedScale(
          {options.nodes, HistoricalStrokeCount(options.nodes), options.seed},
          &document, &scene, &geometry, &cache, &scheduler, &scale, &error)) {
    throw std::runtime_error(error);
  }
  const auto build_end = std::chrono::steady_clock::now();

  IntegratedActionReport actions;
  if (!RunIntegratedActionCycle(options.nodes, scale.historical_strokes,
                                &document, &scene, &geometry, &cache,
                                &scheduler, &actions, &error)) {
    throw std::runtime_error(error);
  }

  const RuntimeScene oracle = SceneCompiler().CompileFull(document);
  const bool equivalent = scene.Digest() == oracle.Digest();
  const size_t estimated_bytes = document.EstimatedBytes() +
                                 scene.EstimatedBytes() +
                                 geometry.document().EstimatedBytes() +
                                 cache.stats().bytes;
  const auto build_ms = std::chrono::duration<double, std::milli>(
      build_end - build_start).count();
  std::ostringstream json;
  json << std::fixed << std::setprecision(3)
       << "{\n"
       << "  \"schema_version\": 1,\n"
       << "  \"poc\": \"POC-03 Integrated Ink\",\n"
       << "  \"seed\": " << options.seed << ",\n"
       << "  \"base_nodes\": " << options.nodes << ",\n"
       << "  \"nodes\": " << options.nodes << ",\n"
       << "  \"historical_strokes\": " << scale.historical_strokes << ",\n"
       << "  \"trace_strokes\": 2,\n"
       << "  \"samples_per_stroke\": 16,\n"
       << "  \"batches_per_stroke\": 4,\n"
       << "  \"vector_strokes\": " << scale.vector_strokes + 1U << ",\n"
       << "  \"dab_strokes\": " << scale.dab_strokes + 1U << ",\n"
       << "  \"ink_document_digest\": \"" << geometry.document().Digest()
       << "\",\n"
       << "  \"vector_trace_stroke_digest\": \""
       << actions.vector_stroke_digest << "\",\n"
       << "  \"dab_trace_stroke_digest\": \""
       << actions.dab_stroke_digest << "\",\n"
       << "  \"document_digest\": \"" << document.Digest() << "\",\n"
       << "  \"scene_digest\": \"" << scene.Digest() << "\",\n"
       << "  \"full_incremental_equivalent\": "
       << (equivalent ? "true" : "false") << ",\n"
       << "  \"maximum_records_touched\": "
       << std::max(scale.maximum_records_touched,
                   actions.maximum_records_touched) << ",\n"
       << "  \"full_fallbacks\": "
       << scale.full_fallbacks + actions.full_fallbacks << ",\n"
       << "  \"maximum_candidates\": " << actions.maximum_candidates << ",\n"
       << "  \"maximum_queue_batches\": "
       << scale.maximum_queue_batches << ",\n"
       << "  \"maximum_pending_callbacks\": "
       << scale.maximum_pending_callbacks << ",\n"
       << "  \"preview_canonical_handoff_frames\": "
       << actions.handoff_frames << ",\n"
       << "  \"cache_invalidations\": " << actions.cache_invalidations
       << ",\n"
       << "  \"build_scale_ms\": " << build_ms << ",\n"
       << "  \"estimated_runtime_mib\": " << Mebibytes(estimated_bytes)
       << ",\n"
       << "  \"process_peak_mib\": " << Mebibytes(estimated_bytes) << ",\n"
       << "  \"actions\": [\"pan\", \"zoom\", \"write-vector\", "
          "\"write-dab\", \"select\", \"drag\"]\n"
       << "}\n";
  if (!options.output.empty()) {
    std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("could not create integrated result");
    }
    output << json.str();
  }
  std::cout << json.str();
  return equivalent && actions.maximum_candidates <= 5000U &&
                 scale.full_fallbacks == 0U && actions.full_fallbacks == 0U &&
                 scheduler.pending_callback_count() == 0U
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}

}  // namespace
}  // namespace canvas::poc03

int main(int argc, char** argv) {
  try {
    return canvas::poc03::Run(canvas::poc03::ParseOptions(argc, argv));
  } catch (const std::exception& exception) {
    std::cerr << "canvas_poc03_integrated_benchmark: " << exception.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
