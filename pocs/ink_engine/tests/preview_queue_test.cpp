#include <gtest/gtest.h>

#include "canvas_poc02/ink_engine.h"
#include "test_support.h"

namespace canvas::poc02 {
namespace {

PreviewStrokeUpdate Update(uint64_t revision, size_t truncate_to,
                           std::vector<float> confirmed,
                           std::vector<float> predicted) {
  PreviewStrokeUpdate update{
      .stroke_id = 77,
      .revision = revision,
      .view_id = 3,
      .viewport_revision = 1,
      .brush = test::VectorBrush(),
      .truncate_confirmed_to = truncate_to,
  };
  for (float x : confirmed) {
    update.confirmed_append.push_back(
        PreviewPrimitive{.position = {x, 0.0F}});
  }
  for (float x : predicted) {
    update.predicted_tail.push_back(
        PreviewPrimitive{.position = {x, 0.0F}});
  }
  return update;
}

TEST(PreviewQueue, CoalescesReplaceableRevisionsAndReplacesPrediction) {
  PreviewUpdateQueue queue;
  ASSERT_EQ(queue.Enqueue(Update(1, 0, {1.0F, 2.0F}, {3.0F})), Status::kOk);
  ASSERT_EQ(queue.Enqueue(Update(2, 2, {2.5F}, {4.0F})), Status::kOk);
  EXPECT_EQ(queue.diagnostics().updates, 1U);
  EXPECT_EQ(queue.diagnostics().coalesced_updates, 1U);
  auto update = queue.Pop();
  ASSERT_TRUE(update.has_value());
  ASSERT_EQ(update->confirmed_append.size(), 3U);
  EXPECT_EQ(update->confirmed_append[2].position.x, 2.5F);
  ASSERT_EQ(update->predicted_tail.size(), 1U);
  EXPECT_EQ(update->predicted_tail[0].position.x, 4.0F);
  EXPECT_EQ(update->revision, 2U);
}

TEST(PreviewQueue, RejectsUnboundedSlowConsumer) {
  PreviewUpdateQueue queue(PreviewQueueLimits{.max_updates = 1,
                                               .max_primitives = 2,
                                               .max_bytes = 4096});
  ASSERT_EQ(queue.Enqueue(Update(1, 0, {1.0F}, {})), Status::kOk);
  PreviewStrokeUpdate incompatible = Update(1, 0, {2.0F}, {});
  incompatible.stroke_id = 88;
  EXPECT_EQ(queue.Enqueue(std::move(incompatible)), Status::kInputOverrun);
  EXPECT_EQ(queue.diagnostics().updates, 1U);
  EXPECT_EQ(queue.diagnostics().overruns, 1U);
}

TEST(PreviewQueue, NeverUnderflowsWhenNewerRevisionRewindsBeforeQueuedBase) {
  PreviewUpdateQueue queue(PreviewQueueLimits{.max_updates = 2,
                                               .max_primitives = 8,
                                               .max_bytes = 4096});
  ASSERT_EQ(queue.Enqueue(Update(1, 2, {3.0F}, {4.0F})), Status::kOk);
  ASSERT_EQ(queue.Enqueue(Update(2, 1, {2.0F}, {5.0F})), Status::kOk);
  EXPECT_EQ(queue.diagnostics().updates, 2U);
  EXPECT_EQ(queue.diagnostics().coalesced_updates, 0U);
  ASSERT_EQ(queue.Pop()->revision, 1U);
  ASSERT_EQ(queue.Pop()->revision, 2U);
}

}  // namespace
}  // namespace canvas::poc02
