#include "test_support.h"

#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

namespace canvas::poc02::test {

PointerSample Sample(uint64_t sequence, float x, float y, float pressure,
                     uint64_t timestamp_us, PointerPhase phase) {
  return PointerSample{.pointer_id = 9,
                       .sample_sequence = sequence,
                       .position = {x, y},
                       .pressure = pressure,
                       .tilt = {0.1F, -0.2F},
                       .contact_size = {2.0F, 2.0F},
                       .timestamp_us = timestamp_us,
                       .phase = phase};
}

PointerSampleBatch Batch(std::vector<PointerSample> samples,
                         AffineTransform transform,
                         uint64_t viewport_revision) {
  return PointerSampleBatch{
      .view_id = 3,
      .viewport_revision = viewport_revision,
      .view_to_world = transform,
      .device = {.device_id = 5,
                 .tool = PointerTool::kPen,
                 .capabilities = kCapabilityPressure | kCapabilityTilt |
                                 kCapabilityHover | kCapabilityEraserTip},
      .samples = std::move(samples),
  };
}

BrushDescriptor VectorBrush() {
  return BrushDescriptor{.type = BrushType::kVector,
                         .size = 8.0F,
                         .spacing = 0.25F,
                         .opacity = 0.9F};
}

BrushDescriptor DabBrush() {
  return BrushDescriptor{.type = BrushType::kDab,
                         .size = 10.0F,
                         .spacing = 0.4F,
                         .opacity = 0.8F,
                         .jitter = 0.2F,
                         .resource_id = "fixture/checker",
                         .resource_content_hash = "d9f00b"};
}

std::string ReadFixture(std::string_view name) {
  const std::string path = std::string(CANVAS_POC02_FIXTURE_DIR) + "/" +
                           std::string(name);
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.good()) << path;
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

AddStrokeOperation RunFixture(std::string_view name, StrokeDocument* document,
                              DefaultPreviewSink* sink) {
  ReplayFixture fixture;
  std::string error;
  EXPECT_EQ(ParseReplayFixture(ReadFixture(name), &fixture, &error), Status::kOk)
      << error;
  AddStrokeOperation operation;
  EXPECT_EQ(RunReplayFixture(fixture, document, sink, &operation, &error), Status::kOk)
      << error;
  return operation;
}

}  // namespace canvas::poc02::test
