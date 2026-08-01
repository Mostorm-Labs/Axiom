#include "canvas/app/embedded_document_open_coordinator.h"

#include <gtest/gtest.h>

namespace {
using Coordinator = canvas::app::EmbeddedDocumentOpenCoordinator;

Coordinator::Candidate candidate(std::uint64_t generation,
                                 std::size_t nodeCount,
                                 std::uint64_t firstToken) {
  return {generation, nodeCount, {{firstToken, "node"}}};
}

TEST(EmbeddedDocumentOpenCoordinatorTest, CommitsOnlyAfterCandidateReady) {
  Coordinator coordinator;
  ASSERT_TRUE(coordinator.begin(candidate(1U, 3U, 11U)));
  EXPECT_EQ(coordinator.state(), Coordinator::State::Pending);
  EXPECT_EQ(coordinator.committedGeneration(), 0U);
  EXPECT_TRUE(coordinator.complete(1U, 11U, true));
  EXPECT_EQ(coordinator.state(), Coordinator::State::Ready);
  EXPECT_EQ(coordinator.committedGeneration(), 1U);
  EXPECT_EQ(coordinator.committedNodeCount(), 3U);
}

TEST(EmbeddedDocumentOpenCoordinatorTest, EmptyCandidateIsReadyImmediately) {
  Coordinator coordinator;
  ASSERT_TRUE(coordinator.begin({7U, 5U, {}}));
  EXPECT_EQ(coordinator.state(), Coordinator::State::Ready);
  EXPECT_EQ(coordinator.pendingGeneration(), 0U);
  EXPECT_EQ(coordinator.committedGeneration(), 7U);
  EXPECT_EQ(coordinator.committedNodeCount(), 5U);
}

TEST(EmbeddedDocumentOpenCoordinatorTest, FailureKeepsPreviousCommit) {
  Coordinator coordinator;
  ASSERT_TRUE(coordinator.begin(candidate(1U, 1U, 11U)));
  ASSERT_TRUE(coordinator.complete(1U, 11U, true));
  ASSERT_TRUE(coordinator.begin(candidate(2U, 2U, 22U)));
  ASSERT_TRUE(coordinator.complete(2U, 22U, false));
  EXPECT_EQ(coordinator.state(), Coordinator::State::Failed);
  EXPECT_EQ(coordinator.committedGeneration(), 1U);
  EXPECT_EQ(coordinator.committedNodeCount(), 1U);
}

TEST(EmbeddedDocumentOpenCoordinatorTest, NewGenerationReplacesPendingCandidate) {
  Coordinator coordinator;
  ASSERT_TRUE(coordinator.begin(candidate(1U, 1U, 11U)));
  ASSERT_TRUE(coordinator.begin(candidate(2U, 2U, 22U)));
  EXPECT_EQ(coordinator.pendingGeneration(), 2U);
  EXPECT_FALSE(coordinator.complete(1U, 11U, true));
  EXPECT_TRUE(coordinator.complete(2U, 22U, true));
  EXPECT_EQ(coordinator.committedGeneration(), 2U);
}

TEST(EmbeddedDocumentOpenCoordinatorTest, CancelAndTimeoutNeverCommit) {
  Coordinator coordinator;
  ASSERT_TRUE(coordinator.begin(candidate(1U, 1U, 11U)));
  EXPECT_TRUE(coordinator.cancel());
  EXPECT_FALSE(coordinator.complete(1U, 11U, true));
  EXPECT_EQ(coordinator.committedGeneration(), 0U);
  ASSERT_TRUE(coordinator.begin(candidate(2U, 1U, 22U)));
  EXPECT_TRUE(coordinator.timeout());
  EXPECT_FALSE(coordinator.complete(2U, 22U, true));
  EXPECT_EQ(coordinator.committedGeneration(), 0U);
}
}  // namespace
