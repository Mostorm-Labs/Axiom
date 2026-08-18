#include <bit>
#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "canvas_poc02/ink_engine.h"
#include "foundation.h"
#include "test_support.h"

namespace canvas::poc02 {
namespace {

TEST(NumericConformance, CanonicalBoundaryCorpusHasReviewedBits) {
  EXPECT_EQ(std::bit_cast<uint32_t>(internal::CanonicalFloat(-0.0)),
            0x00000000U);
  EXPECT_EQ(std::bit_cast<uint32_t>(internal::CanonicalFloat(
                static_cast<double>(std::numeric_limits<float>::denorm_min()))),
            0x00000001U);
  EXPECT_EQ(std::bit_cast<uint32_t>(
                internal::CanonicalFloat(1.000000059604644775390625)),
            0x3f800000U);
  EXPECT_EQ(std::bit_cast<uint32_t>(internal::CanonicalFloat(std::nextafter(
                1.000000059604644775390625, 2.0))),
            0x3f800001U);
  EXPECT_THROW(internal::CanonicalFloat(16777217.0), std::invalid_argument);
  EXPECT_THROW(internal::CanonicalFloat(
                   std::numeric_limits<double>::infinity()),
               std::invalid_argument);
  EXPECT_EQ(NumericConformanceDigest().size(), 32U);
}

TEST(InkEngine, VectorProcessingIsIncrementalAndPredictionRollsBack) {
  DefaultPreviewSink sink;
  StrokeSession session(71, 9, test::VectorBrush(), sink);
  ASSERT_EQ(session.Begin(test::Batch({test::Sample(0, 0, 0, 0.2F, 1000,
                                                   PointerPhase::kDown),
                                      test::Sample(1, 10, 0, 0.5F, 5000)})),
            Status::kOk);
  ASSERT_GE(session.incremental_work_count(), 2U);
  const auto* first = sink.Find(71);
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->predicted.size(), 1U);
  const Vec2 old_prediction = first->predicted.front().position;

  ASSERT_EQ(session.Push(test::Batch({test::Sample(2, 10, 10, 0.8F, 9000)})),
            Status::kOk);
  const auto* second = sink.Find(71);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->revision, 2U);
  ASSERT_EQ(second->predicted.size(), 1U);
  EXPECT_NE(second->predicted.front().position, old_prediction);
  EXPECT_EQ(session.confirmed_input_count(), 3U);
  EXPECT_GE(session.incremental_work_count(), 3U);

  Stroke stroke;
  ASSERT_EQ(session.End(&stroke), Status::kOk);
  EXPECT_EQ(stroke.confirmed_samples.size(), 3U);
  EXPECT_TRUE(stroke.dabs.empty());
  EXPECT_FALSE(stroke.vector_points.empty());
  for (const VectorPoint& point : stroke.vector_points) {
    EXPECT_NE(point.position, second->predicted.front().position);
  }
}

TEST(InkEngine, DabRandomnessIsRepeatableAndDomainSeparated) {
  auto run = [](StrokeId id, BrushDescriptor brush) {
    DefaultPreviewSink sink;
    StrokeSession session(id, 9, brush, sink);
    EXPECT_EQ(session.Begin(test::Batch({test::Sample(0, 1, 2, 0.3F, 1000,
                                                     PointerPhase::kDown),
                                        test::Sample(1, 5, 8, 0.8F, 6000)})),
              Status::kOk);
    Stroke result;
    EXPECT_EQ(session.End(&result), Status::kOk);
    return StrokeDigest(result);
  };
  const std::string baseline = run(99, test::DabBrush());
  EXPECT_EQ(run(99, test::DabBrush()), baseline);
  EXPECT_NE(run(100, test::DabBrush()), baseline);
  BrushDescriptor changed = test::DabBrush();
  changed.resource_content_hash = "other";
  EXPECT_NE(run(99, changed), baseline);
  changed = test::DabBrush();
  changed.algorithm_version = 2;
  DefaultPreviewSink sink;
  StrokeSession rejected(99, 9, changed, sink);
  EXPECT_EQ(rejected.Begin(test::Batch({test::Sample(0, 1, 2, 0.3F, 1000)})),
            Status::kUnsupportedVersion);
}

TEST(InkEngine, DabSpacingChangesCanonicalPlacement) {
  auto run = [](float spacing) {
    BrushDescriptor brush = test::DabBrush();
    brush.spacing = spacing;
    DefaultPreviewSink sink;
    StrokeSession session(102, 9, brush, sink);
    EXPECT_EQ(session.Begin(test::Batch({test::Sample(0, 0, 0, 0.5F, 0),
                                        test::Sample(1, 2, 0, 0.5F, 4000),
                                        test::Sample(2, 4, 0, 0.5F, 8000),
                                        test::Sample(3, 8, 0, 0.5F, 12000)})),
              Status::kOk);
    Stroke stroke;
    EXPECT_EQ(session.End(&stroke), Status::kOk);
    return stroke;
  };
  const Stroke dense = run(0.2F);
  const Stroke sparse = run(0.8F);
  EXPECT_GT(dense.dabs.size(), sparse.dabs.size());
  EXPECT_NE(StrokeDigest(dense), StrokeDigest(sparse));
}

TEST(InkEngine, RejectsNonFiniteAndInvalidTransformsAtomically) {
  DefaultPreviewSink sink;
  StrokeSession session(17, 9, test::VectorBrush(), sink);
  PointerSample invalid = test::Sample(0, 0, 0, 0.5F, 1000);
  invalid.position.x = std::numeric_limits<float>::infinity();
  EXPECT_EQ(session.Begin(test::Batch({invalid})), Status::kInvalidArgument);
  EXPECT_FALSE(session.active());
  EXPECT_EQ(sink.Find(17), nullptr);

  AffineTransform singular{.m00 = 1.0F, .m01 = 2.0F,
                           .m10 = 2.0F, .m11 = 4.0F};
  EXPECT_EQ(session.Begin(test::Batch({test::Sample(0, 0, 0, 0.5F, 1000)},
                                     singular)),
            Status::kInvalidArgument);
}

TEST(InkEngine, CanonicalizesNegativeZeroAndMissingCapabilities) {
  DefaultPreviewSink sink;
  StrokeSession session(18, 9, test::VectorBrush(), sink);
  PointerSample sample = test::Sample(0, -0.0F, -0.0F, 0.0F, 1000,
                                      PointerPhase::kDown);
  auto batch = test::Batch({sample});
  batch.device.capabilities = 0;
  ASSERT_EQ(session.Begin(batch), Status::kOk);
  Stroke stroke;
  ASSERT_EQ(session.End(&stroke), Status::kOk);
  ASSERT_EQ(stroke.confirmed_samples.size(), 1U);
  EXPECT_FALSE(std::signbit(stroke.confirmed_samples[0].position.x));
  EXPECT_FALSE(std::signbit(stroke.confirmed_samples[0].position.y));
  EXPECT_EQ(stroke.confirmed_samples[0].pressure, 0.5F);
  EXPECT_EQ(stroke.confirmed_samples[0].tilt, Vec2{});
}

TEST(InkEngine, LongStrokeDoesMostWorkDuringPush) {
  DefaultPreviewSink sink;
  StrokeSession session(300, 9, test::VectorBrush(), sink);
  ASSERT_EQ(session.Begin(test::Batch({test::Sample(0, 0, 0, 0.5F, 0,
                                                   PointerPhase::kDown)})),
            Status::kOk);
  for (uint64_t index = 1; index <= 7200; ++index) {
    ASSERT_EQ(session.Push(test::Batch({test::Sample(
                  index, static_cast<float>(index), static_cast<float>(index % 17),
                  0.5F, index * 4167)})), Status::kOk);
  }
  const size_t before_end = session.incremental_work_count();
  Stroke stroke;
  ASSERT_EQ(session.End(&stroke), Status::kOk);
  EXPECT_EQ(session.incremental_work_count(), before_end);
  EXPECT_GE(before_end, 7201U);
  EXPECT_EQ(stroke.confirmed_samples.size(), 7201U);
}

TEST(InkEngine, SixtySecond240HzReplayPreservesEveryConfirmedSample) {
  StrokeDocument document;
  DefaultPreviewSink sink;
  InputRouter router(document, sink,
      QueueLimits{.max_batches = 8,
                  .max_samples = 512,
                  .max_bytes = 128U * 1024U,
                  .max_batch_samples = 64,
                  .max_oldest_sample_age_us = 250000});
  ASSERT_EQ(router.Begin(400, 9, test::VectorBrush(),
                         test::Batch({test::Sample(0, 0, 0, 0.5F, 0,
                                                          PointerPhase::kDown)})),
            Status::kOk);
  constexpr uint64_t kTotalSamples = 60U * 240U;
  for (uint64_t start = 1; start < kTotalSamples; start += 32) {
    std::vector<PointerSample> samples;
    const uint64_t end = std::min<uint64_t>(start + 32, kTotalSamples);
    for (uint64_t index = start; index < end; ++index) {
      samples.push_back(test::Sample(index, static_cast<float>(index % 800),
                                     static_cast<float>((index / 800) % 600),
                                     static_cast<float>(index % 101) / 100.0F,
                                     index * 4167,
                                     index + 1 == kTotalSamples
                                         ? PointerPhase::kUp
                                         : PointerPhase::kMove));
    }
    const uint64_t now = samples.back().timestamp_us;
    ASSERT_EQ(router.Submit(test::Batch(std::move(samples)), now), Status::kOk);
    ASSERT_EQ(router.Drain(now), Status::kOk);
    EXPECT_LE(router.queue_diagnostics().oldest_sample_age_us, 250000U);
  }
  AddStrokeOperation operation;
  ASSERT_EQ(router.End(1, &operation), Status::kOk);
  EXPECT_EQ(operation.stroke.confirmed_samples.size(), kTotalSamples);
  EXPECT_EQ(operation.stroke.confirmed_samples.front().timestamp_us, 0U);
  EXPECT_EQ(operation.stroke.confirmed_samples.back().timestamp_us,
            (kTotalSamples - 1) * 4167);
  StrokeDocument replayed;
  ASSERT_EQ(replayed.Apply(operation), Status::kOk);
  EXPECT_EQ(replayed.Digest(), document.Digest());
}

}  // namespace
}  // namespace canvas::poc02
