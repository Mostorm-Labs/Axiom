#include "canvas/app/embedded_load_batch.h"

#include <gtest/gtest.h>

namespace {
using canvas::app::EmbeddedLoadBatch;

TEST(EmbeddedLoadBatchTest, AllLoadsMustBeReadyBeforeCommit) {
  auto batch = EmbeddedLoadBatch::create(7U, {{11U, "a"}, {12U, "b"}});
  ASSERT_TRUE(batch);
  EXPECT_EQ(batch->state(), EmbeddedLoadBatch::State::Pending);
  EXPECT_TRUE(batch->complete(11U, 7U, EmbeddedLoadBatch::Completion::Ready));
  EXPECT_EQ(batch->state(), EmbeddedLoadBatch::State::Pending);
  EXPECT_TRUE(batch->complete(12U, 7U, EmbeddedLoadBatch::Completion::Ready));
  EXPECT_EQ(batch->state(), EmbeddedLoadBatch::State::Ready);
  EXPECT_FALSE(batch->complete(12U, 7U, EmbeddedLoadBatch::Completion::Ready));
}

TEST(EmbeddedLoadBatchTest, FailureCancelAndTimeoutAreTerminal) {
  auto failed = EmbeddedLoadBatch::create(9U, {{1U, "node"}});
  ASSERT_TRUE(failed);
  EXPECT_TRUE(failed->complete(1U, 9U, EmbeddedLoadBatch::Completion::Failed));
  EXPECT_EQ(failed->state(), EmbeddedLoadBatch::State::Failed);
  EXPECT_FALSE(failed->cancel());

  auto cancelled = EmbeddedLoadBatch::create(10U, {{2U, "node"}});
  ASSERT_TRUE(cancelled);
  EXPECT_TRUE(cancelled->cancel());
  EXPECT_FALSE(cancelled->timeout());

  auto timedOut = EmbeddedLoadBatch::create(11U, {{3U, "node"}});
  ASSERT_TRUE(timedOut);
  EXPECT_TRUE(timedOut->timeout());
  EXPECT_FALSE(timedOut->complete(3U, 11U, EmbeddedLoadBatch::Completion::Ready));
}

TEST(EmbeddedLoadBatchTest, RejectsInvalidGenerationAndTokens) {
  EXPECT_FALSE(EmbeddedLoadBatch::create(0U, {}));
  EXPECT_FALSE(EmbeddedLoadBatch::create(1U, {{0U, "node"}}));
  EXPECT_FALSE(EmbeddedLoadBatch::create(1U, {{2U, "a"}, {2U, "b"}}));
}
}  // namespace
