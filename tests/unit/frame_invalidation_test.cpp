#include "platform/frame_invalidation.h"

#include <gtest/gtest.h>

namespace canvas::platform {
namespace {

TEST(FrameInvalidation, CoalescesRepeatedRequestsUntilTheFrameBegins) {
  FrameInvalidation invalidation;

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_FALSE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.hasPendingFrame());

  EXPECT_TRUE(invalidation.beginFrame());
  EXPECT_FALSE(invalidation.hasPendingFrame());
  EXPECT_TRUE(invalidation.isFrameInProgress());
  invalidation.completeFrame();
  EXPECT_FALSE(invalidation.isFrameInProgress());
}

TEST(FrameInvalidation, NewInvalidationDuringRenderingSchedulesAnotherFrame) {
  FrameInvalidation invalidation;

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_FALSE(invalidation.requestFrame());
  invalidation.completeFrame();
  EXPECT_TRUE(invalidation.beginFrame());
  invalidation.completeFrame();
  EXPECT_FALSE(invalidation.beginFrame());
}

TEST(FrameInvalidation, ClaimGuardRejectsReentrantBeginWithoutConsumingPending) {
  FrameInvalidation invalidation;

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());
  EXPECT_TRUE(invalidation.requestFrame());

  // This models drawIfNeeded re-entering while nextDrawable is being called.
  // The inner claim must not consume the outer frame's pending invalidation.
  EXPECT_FALSE(invalidation.beginFrame());
  EXPECT_TRUE(invalidation.hasPendingFrame());

  invalidation.completeFrame();
  EXPECT_TRUE(invalidation.beginFrame());
}

TEST(FrameInvalidation,
     AbandonedDrawableRestoresClaimWithoutSchedulingItsOwnRetry) {
  FrameInvalidation invalidation;

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());
  invalidation.abandonFrame();
  EXPECT_TRUE(invalidation.hasPendingFrame());
  EXPECT_FALSE(invalidation.isFrameInProgress());

  // A drawable shortage must not turn one drawRect: call into a recursive
  // setNeedsDisplay: loop. A later resize/invalidate asks AppKit for one retry.
  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_FALSE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());
}

TEST(FrameInvalidation, FailedEncodingRestoresClaimForAnExternalRetry) {
  FrameInvalidation invalidation;

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());

  invalidation.failFrame();
  EXPECT_TRUE(invalidation.hasPendingFrame());
  EXPECT_FALSE(invalidation.isFrameInProgress());
  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());
}

TEST(FrameInvalidation, FailurePreservesARequestMadeDuringRendering) {
  FrameInvalidation invalidation;

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());
  EXPECT_TRUE(invalidation.requestFrame());

  invalidation.failFrame();
  EXPECT_FALSE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());
}

TEST(FrameInvalidation, ResetDropsAStaleRequestWhenTheHostDetaches) {
  FrameInvalidation invalidation;

  EXPECT_TRUE(invalidation.requestFrame());
  invalidation.reset();
  EXPECT_FALSE(invalidation.hasPendingFrame());
  EXPECT_FALSE(invalidation.beginFrame());
  EXPECT_TRUE(invalidation.requestFrame());
}

TEST(FrameInvalidation, ResetMakesAnOldClaimCompletionOrFailureANoOp) {
  FrameInvalidation invalidation;

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());
  const FrameInvalidation::FrameId oldFrame = invalidation.activeFrameId();

  invalidation.reset();
  invalidation.completeFrame(oldFrame);
  invalidation.abandonFrame(oldFrame);
  EXPECT_FALSE(invalidation.hasPendingFrame());
  EXPECT_FALSE(invalidation.isFrameInProgress());

  EXPECT_TRUE(invalidation.requestFrame());
  EXPECT_TRUE(invalidation.beginFrame());
  const FrameInvalidation::FrameId newFrame = invalidation.activeFrameId();
  EXPECT_NE(oldFrame, newFrame);
  invalidation.abandonFrame(oldFrame);
  EXPECT_TRUE(invalidation.isFrameInProgress());
  invalidation.completeFrame(newFrame);
}

}  // namespace
}  // namespace canvas::platform
