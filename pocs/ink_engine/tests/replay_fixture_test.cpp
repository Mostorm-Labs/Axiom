#include <gtest/gtest.h>

#include "canvas_poc02/ink_engine.h"
#include "test_support.h"

namespace canvas::poc02 {
namespace {

TEST(ReplayFixture, VectorAndDabAreDeterministicAcrossTenRuns) {
  for (const std::string fixture_name : {"vector-pressure.ndjson", "dab-turn.ndjson"}) {
    std::string document_digest;
    std::string stroke_digest;
    std::string preview_digest;
    for (int run = 0; run < 10; ++run) {
      StrokeDocument document;
      DefaultPreviewSink sink;
      const AddStrokeOperation operation =
          test::RunFixture(fixture_name, &document, &sink);
      if (run == 0) {
        document_digest = document.Digest();
        stroke_digest = StrokeDigest(operation.stroke);
        preview_digest = sink.ModelDigest();
      } else {
        EXPECT_EQ(document.Digest(), document_digest);
        EXPECT_EQ(StrokeDigest(operation.stroke), stroke_digest);
        EXPECT_EQ(sink.ModelDigest(), preview_digest);
      }
    }
  }
}

TEST(ReplayFixture, ViewTransformProducesCanonicalWorldCoordinates) {
  StrokeDocument document;
  DefaultPreviewSink sink;
  const AddStrokeOperation operation =
      test::RunFixture("vector-pressure.ndjson", &document, &sink);
  ASSERT_FALSE(operation.stroke.confirmed_samples.empty());
  EXPECT_EQ(operation.stroke.confirmed_samples.front().position, (Vec2{10.0F, 20.0F}));
  EXPECT_EQ(operation.stroke.confirmed_samples.back().position, (Vec2{70.0F, 50.0F}));
}

TEST(ReplayFixture, DprZoomAndPanDoNotEnterCanonicalWorldDigest) {
  auto run = [](AffineTransform transform, std::vector<Vec2> points,
                uint64_t viewport_revision) {
    ReplayFixture fixture{
        .stroke_id = 909,
        .pointer_id = 9,
        .brush = test::VectorBrush(),
    };
    std::vector<PointerSample> samples;
    for (size_t index = 0; index < points.size(); ++index) {
      samples.push_back(test::Sample(
          index, points[index].x, points[index].y, 0.5F, index * 4000,
          index == 0 ? PointerPhase::kDown
                     : index + 1 == points.size() ? PointerPhase::kUp
                                                  : PointerPhase::kMove));
    }
    fixture.batches.push_back(
        test::Batch(std::move(samples), transform, viewport_revision));
    StrokeDocument document;
    DefaultPreviewSink sink;
    AddStrokeOperation operation;
    std::string error;
    EXPECT_EQ(RunReplayFixture(fixture, &document, &sink, &operation, &error),
              Status::kOk) << error;
    return StrokeDigest(operation.stroke);
  };
  const std::string baseline = run({}, {{10, 20}, {30, 40}, {50, 60}}, 1);
  EXPECT_EQ(run(AffineTransform{.m00 = 2.0F, .m11 = 2.0F},
                {{5, 10}, {15, 20}, {25, 30}}, 2), baseline);
  EXPECT_EQ(run(AffineTransform{.m00 = 1.25F, .m11 = 1.25F,
                                .tx = -2.5F, .ty = 7.5F},
                {{10, 10}, {26, 26}, {42, 42}}, 3), baseline);
}

}  // namespace
}  // namespace canvas::poc02
