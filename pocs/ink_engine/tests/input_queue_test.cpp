#include <gtest/gtest.h>

#include "canvas_poc02/ink_engine.h"
#include "test_support.h"

namespace canvas::poc02 {
namespace {

TEST(InputQueue, MergesOnlyCompatibleConsecutiveBatches) {
  PointerBatchQueue queue(QueueLimits{.max_batches = 4,
                                      .max_samples = 16,
                                      .max_bytes = 8192,
                                      .max_batch_samples = 8,
                                      .max_oldest_sample_age_us = 10000});
  EXPECT_EQ(queue.Enqueue(test::Batch({test::Sample(0, 0, 0, 0.5F, 1000)}), 1000),
            Status::kOk);
  EXPECT_EQ(queue.Enqueue(test::Batch({test::Sample(1, 1, 1, 0.5F, 2000)}), 2000),
            Status::kOk);
  EXPECT_EQ(queue.diagnostics().batches, 1U);
  EXPECT_EQ(queue.diagnostics().samples, 2U);
  EXPECT_EQ(queue.diagnostics().merged_batches, 1U);
  auto batch = queue.Pop(2000);
  ASSERT_TRUE(batch.has_value());
  EXPECT_EQ(batch->samples.size(), 2U);
}

TEST(InputQueue, OverrunCancelsStrokeWithoutPartialDocument) {
  StrokeDocument document;
  DefaultPreviewSink sink;
  InputRouter router(document, sink,
                     QueueLimits{.max_batches = 1,
                                 .max_samples = 2,
                                 .max_bytes = 4096,
                                 .max_batch_samples = 2,
                                 .max_oldest_sample_age_us = 10000});
  ASSERT_EQ(router.Begin(20, 9, test::VectorBrush(),
                         test::Batch({test::Sample(0, 0, 0, 0.5F, 1000,
                                                          PointerPhase::kDown)})),
            Status::kOk);
  EXPECT_EQ(router.Submit(test::Batch({test::Sample(1, 1, 1, 0.5F, 2000),
                                      test::Sample(2, 2, 2, 0.5F, 3000)}), 3000),
            Status::kOk);
  EXPECT_EQ(router.Submit(test::Batch({test::Sample(3, 3, 3, 0.5F, 4000)}), 4000),
            Status::kInputOverrun);
  EXPECT_EQ(document.stroke_count(), 0U);
  EXPECT_EQ(document.revision(), 0U);
  EXPECT_EQ(router.active_session(), nullptr);
  EXPECT_EQ(sink.Find(20), nullptr);
  EXPECT_EQ(router.queue_diagnostics().overruns, 1U);
}

TEST(InputQueue, RejectsSequenceGapWithoutChangingConfirmedPrefix) {
  StrokeDocument document;
  DefaultPreviewSink sink;
  InputRouter router(document, sink);
  ASSERT_EQ(router.Begin(21, 9, test::VectorBrush(),
                         test::Batch({test::Sample(0, 0, 0, 0.5F, 1000)})),
            Status::kOk);
  ASSERT_EQ(router.Submit(test::Batch({test::Sample(2, 2, 2, 0.5F, 3000)}), 3000),
            Status::kOk);
  EXPECT_EQ(router.Drain(3000), Status::kSequenceError);
  EXPECT_EQ(document.stroke_count(), 0U);
  EXPECT_EQ(sink.Find(21), nullptr);
}

TEST(InputRouter, CanonicalHandoffRequiresVisibleAcknowledgement) {
  StrokeDocument document;
  DefaultPreviewSink sink;
  InputRouter router(document, sink);
  ASSERT_EQ(router.Begin(22, 9, test::VectorBrush(),
                         test::Batch({test::Sample(0, 0, 0, 0.5F, 1000)})),
            Status::kOk);
  AddStrokeOperation operation;
  ASSERT_EQ(router.End(1, &operation), Status::kOk);
  const auto* state = sink.Find(22);
  ASSERT_NE(state, nullptr);
  EXPECT_TRUE(state->committed);
  EXPECT_FALSE(state->visible);
  EXPECT_EQ(router.AcknowledgeCanonicalVisible(22, document.revision()), Status::kOk);
  state = sink.Find(22);
  ASSERT_NE(state, nullptr);
  EXPECT_TRUE(state->visible);
  EXPECT_EQ(sink.events().back().type, PreviewEventType::kCanonicalVisible);
}

TEST(InputRouter, PalmHoverAndEraserTipStayOutsideInkOperation) {
  for (int scenario = 0; scenario < 3; ++scenario) {
    StrokeDocument document;
    DefaultPreviewSink sink;
    InputRouter router(document, sink);
    auto batch = test::Batch({test::Sample(0, 1, 1, 0.5F, 1000)});
    if (scenario == 0) batch.device.platform_classified_palm = true;
    if (scenario == 1) batch.device.eraser_tip = true;
    if (scenario == 2) batch.samples[0].phase = PointerPhase::kHover;
    EXPECT_EQ(router.Begin(100 + scenario, 9, test::VectorBrush(), batch),
              Status::kInvalidArgument);
    EXPECT_EQ(document.stroke_count(), 0U);
    EXPECT_TRUE(sink.events().empty());
  }
}

}  // namespace
}  // namespace canvas::poc02
