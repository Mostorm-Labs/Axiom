#include <gtest/gtest.h>

#include "canvas_poc02/ink_engine.h"

namespace canvas::poc02 {
namespace {

TEST(FrameScheduler, BurstInvalidationsProduceOneCallbackWithLatestRevisions) {
  DeterministicFrameScheduler scheduler;
  scheduler.SetTargetGeneration(4, 7);
  for (uint64_t revision = 1; revision <= 100; ++revision) {
    scheduler.Invalidate(FrameInvalidation{
        .view_id = 4,
        .reasons = static_cast<uint32_t>(FrameInvalidationReason::kPreview),
        .minimum_document_revision = revision / 2,
        .minimum_preview_revision = revision,
        .target_generation = 7});
  }
  EXPECT_EQ(scheduler.pending_callback_count(4), 1U);
  auto frame = scheduler.BeginFrame(4);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->minimum_document_revision, 50U);
  EXPECT_EQ(frame->minimum_preview_revision, 100U);
  EXPECT_EQ(scheduler.pending_callback_count(4), 0U);
}

TEST(FrameScheduler, StaleTargetNeverPresents) {
  DeterministicFrameScheduler scheduler;
  scheduler.SetTargetGeneration(4, 8);
  EXPECT_EQ(scheduler.Present(PresentedFrame{.view_id = 4,
                                             .document_revision = 1,
                                             .preview_revision = 2,
                                             .target_generation = 7}),
            Status::kStaleGeneration);
  EXPECT_EQ(scheduler.LastPresented(4), nullptr);
  EXPECT_EQ(scheduler.Present(PresentedFrame{.view_id = 4,
                                             .document_revision = 1,
                                             .preview_revision = 2,
                                             .target_generation = 8}),
            Status::kOk);
  ASSERT_NE(scheduler.LastPresented(4), nullptr);
  EXPECT_EQ(scheduler.LastPresented(4)->target_generation, 8U);
}

}  // namespace
}  // namespace canvas::poc02
