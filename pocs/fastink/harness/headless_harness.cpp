#include "arc/arc.hpp"
#include "canvas_poc02/ink_engine.h"
#include "poc02_adapter.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot open fixture: " + path);
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

struct Result {
  std::string document;
  std::string stroke;
  std::string preview;
  size_t events = 0;
  std::vector<canvas::poc02::PreviewEvent> default_events;
  std::vector<arc::TraceEvent> arc_events;
};

Result RunDefault(const canvas::poc02::ReplayFixture& fixture) {
  canvas::poc02::StrokeDocument document;
  canvas::poc02::DefaultPreviewSink sink;
  canvas::poc02::AddStrokeOperation operation;
  std::string error;
  if (canvas::poc02::RunReplayFixture(fixture, &document, &sink, &operation,
                                      &error) != canvas::poc02::Status::kOk) {
    throw std::runtime_error("default replay failed: " + error);
  }
  return {.document = document.Digest(),
          .stroke = canvas::poc02::StrokeDigest(operation.stroke),
          .preview = sink.ModelDigest(),
          .events = sink.events().size(),
          .default_events = sink.events()};
}

Result RunArc(const canvas::poc02::ReplayFixture& fixture) {
  arc::Bridge bridge(nullptr, arc::CreateNullBackend());
  const arc_preview_target_v0 target{
      .struct_size = sizeof(arc_preview_target_v0),
      .abi_version = ARC_ABI_VERSION,
      .platform_kind = ARC_PLATFORM_HEADLESS,
      .target_id = 1,
      .target_generation = 1,
      .width_pixels = 800,
      .height_pixels = 600,
      .device_pixel_ratio = 1.0F};
  if (bridge.Attach(target) != arc::Status::kOk) {
    throw std::runtime_error("Arc attach failed");
  }
  auto sink = canvas::poc06::CreateArcPreviewAdapter(bridge);
  canvas::poc02::StrokeDocument document;
  canvas::poc02::AddStrokeOperation operation;
  canvas::poc02::InputRouter router(document, *sink);
  auto status = router.Begin(fixture.stroke_id, fixture.pointer_id, fixture.brush,
                             fixture.batches.front());
  for (size_t index = 1;
       status == canvas::poc02::Status::kOk && index < fixture.batches.size();
       ++index) {
    const auto& batch = fixture.batches[index];
    status = router.Submit(batch, batch.samples.back().timestamp_us);
    if (status == canvas::poc02::Status::kOk) {
      status = router.Drain(batch.samples.back().timestamp_us);
    }
  }
  if (status == canvas::poc02::Status::kOk) status = router.End(
      fixture.operation_sequence, &operation);
  if (status == canvas::poc02::Status::kOk) {
    status = router.AcknowledgeCanonicalVisible(operation.stroke.id,
                                                document.revision());
  }
  if (status != canvas::poc02::Status::kOk) {
    throw std::runtime_error("Arc replay failed: " +
                             std::string(canvas::poc02::StatusName(status)));
  }
  const arc::Diagnostics& diagnostics = bridge.diagnostics();
  return {.document = document.Digest(),
          .stroke = canvas::poc02::StrokeDigest(operation.stroke),
          .preview = std::to_string(diagnostics.accepted_updates) + ":" +
                     std::to_string(diagnostics.retired_strokes),
          .events = diagnostics.begun_strokes + diagnostics.accepted_updates +
                    diagnostics.canonical_commits + diagnostics.canonical_visible,
          .arc_events = bridge.trace()};
}

}  // namespace

int main() {
  try {
    const auto fixture = std::string(CANVAS_POC06_FIXTURE_DIR) +
                         "/vector-pressure.ndjson";
    canvas::poc02::ReplayFixture parsed;
    std::string error;
    if (canvas::poc02::ParseReplayFixture(ReadFile(fixture), &parsed, &error) !=
        canvas::poc02::Status::kOk) {
      throw std::runtime_error("fixture parse failed: " + error);
    }
    const Result expected = RunDefault(parsed);
    const Result actual = RunArc(parsed);
    bool trace_equal = expected.default_events.size() == actual.arc_events.size();
    if (trace_equal) {
      for (size_t i = 0; i < expected.default_events.size(); ++i) {
        const auto& left = expected.default_events[i];
        const auto& right = actual.arc_events[i];
        if (static_cast<uint8_t>(left.type) != static_cast<uint8_t>(right.type) ||
            left.stroke_id != right.stroke_id || left.revision != right.preview_revision ||
            left.document_revision != right.document_revision) {
          trace_equal = false;
          break;
        }
      }
    }
    if (expected.document != actual.document || expected.stroke != actual.stroke ||
        expected.events != actual.events || !trace_equal) {
      std::cerr << "Arc/default equivalence failed\n"
                << "default document=" << expected.document
                << " arc document=" << actual.document << "\n"
                << "default stroke=" << expected.stroke
                << " arc stroke=" << actual.stroke << "\n"
                << "default events=" << expected.events
                << " arc updates=" << actual.events << " trace_equal="
                << trace_equal << "\n";
      return 1;
    }
    std::cout << "{\"schema\":1,\"document_digest\":\""
              << actual.document << "\",\"stroke_digest\":\""
              << actual.stroke << "\",\"arc_updates\":" << actual.events
              << ",\"fallback_activations\":0}\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
