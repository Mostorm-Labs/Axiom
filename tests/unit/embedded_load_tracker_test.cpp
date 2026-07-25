#include "canvas/app/embedded_load_tracker.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <new>
#include <string>

namespace canvas::app {

class EmbeddedLoadTrackerTestAccess {
 public:
  static EmbeddedLoadTracker withTokenAndResource(
      EmbeddedLoadTracker::Token nextToken,
      std::pmr::memory_resource& resource) {
    return EmbeddedLoadTracker(nextToken, resource);
  }
};

}  // namespace canvas::app

namespace {

using canvas::app::EmbeddedLoadTracker;
using canvas::app::EmbeddedLoadTrackerTestAccess;

class ThrowNextAllocationResource final : public std::pmr::memory_resource {
 public:
  void failNextAllocation() noexcept { failNext_ = true; }

 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (failNext_) {
      failNext_ = false;
      throw std::bad_alloc{};
    }
    return upstream_->allocate(bytes, alignment);
  }

  void do_deallocate(void* pointer, std::size_t bytes,
                     std::size_t alignment) override {
    upstream_->deallocate(pointer, bytes, alignment);
  }

  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }

  std::pmr::memory_resource* upstream_ = std::pmr::new_delete_resource();
  bool failNext_ = false;
};

TEST(EmbeddedLoadTrackerTest,
     GeneratesNonZeroMonotonicTokensAndPreservesTheOrigin) {
  EmbeddedLoadTracker tracker;

  const auto first = tracker.begin("node-a", "request-a", 7U, 11U);
  const auto second = tracker.begin("node-b", "request-b", 8U, 11U);

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(*first, 0U);
  EXPECT_LT(*first, *second);

  const auto record = tracker.consume(*first, 11U);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->token, *first);
  EXPECT_EQ(record->nodeId, "node-a");
  EXPECT_EQ(record->requestId, "request-a");
  EXPECT_EQ(record->connectionId, 7U);
  EXPECT_EQ(record->documentGeneration, 11U);
}

TEST(EmbeddedLoadTrackerTest, ConsumeTakesARecordOnlyOnce) {
  EmbeddedLoadTracker tracker;
  const auto token = tracker.begin("node", "request", 4U, 12U);
  ASSERT_TRUE(token);

  EXPECT_TRUE(tracker.consume(*token, 12U));
  EXPECT_FALSE(tracker.consume(*token, 12U));
}

TEST(EmbeddedLoadTrackerTest, GenerationMismatchStillDeletesTheRecord) {
  EmbeddedLoadTracker tracker;
  const auto token = tracker.begin("node", "request", 4U, 12U);
  ASSERT_TRUE(token);

  EXPECT_FALSE(tracker.consume(*token, 13U));
  EXPECT_FALSE(tracker.consume(*token, 12U));
}

TEST(EmbeddedLoadTrackerTest, CancelsOneTokenWithoutAffectingOthers) {
  EmbeddedLoadTracker tracker;
  const auto cancelled = tracker.begin("node-a", "request-a", 1U, 20U);
  const auto retained = tracker.begin("node-b", "request-b", 1U, 20U);
  ASSERT_TRUE(cancelled);
  ASSERT_TRUE(retained);

  EXPECT_TRUE(tracker.cancel(*cancelled));
  EXPECT_FALSE(tracker.cancel(*cancelled));
  EXPECT_FALSE(tracker.consume(*cancelled, 20U));
  EXPECT_TRUE(tracker.consume(*retained, 20U));
}

TEST(EmbeddedLoadTrackerTest, CancelsEveryRecordForANode) {
  EmbeddedLoadTracker tracker;
  const auto first = tracker.begin("same-node", "request-a", 1U, 30U);
  const auto second = tracker.begin("same-node", "request-b", 2U, 31U);
  const auto other = tracker.begin("other-node", "request-c", 2U, 31U);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_TRUE(other);

  EXPECT_EQ(tracker.cancelNode("same-node"), 2U);
  EXPECT_EQ(tracker.cancelNode("same-node"), 0U);
  EXPECT_FALSE(tracker.consume(*first, 30U));
  EXPECT_FALSE(tracker.consume(*second, 31U));
  EXPECT_TRUE(tracker.consume(*other, 31U));
}

TEST(EmbeddedLoadTrackerTest, CancelsEveryRecordForADocumentGeneration) {
  EmbeddedLoadTracker tracker;
  const auto first = tracker.begin("node-a", "request-a", 1U, 40U);
  const auto second = tracker.begin("node-b", "request-b", 2U, 40U);
  const auto other = tracker.begin("node-c", "request-c", 2U, 41U);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_TRUE(other);

  EXPECT_EQ(tracker.cancelGeneration(40U), 2U);
  EXPECT_EQ(tracker.cancelGeneration(40U), 0U);
  EXPECT_FALSE(tracker.consume(*first, 40U));
  EXPECT_FALSE(tracker.consume(*second, 40U));
  EXPECT_TRUE(tracker.consume(*other, 41U));
}

TEST(EmbeddedLoadTrackerTest, CancelAllEmptiesTheTracker) {
  EmbeddedLoadTracker tracker;
  const auto first = tracker.begin("node-a", "request-a", 1U, 50U);
  const auto second = tracker.begin("node-b", "request-b", 2U, 51U);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  tracker.cancelAll();

  EXPECT_FALSE(tracker.consume(*first, 50U));
  EXPECT_FALSE(tracker.consume(*second, 51U));
}

TEST(EmbeddedLoadTrackerTest, StaleTokenCannotAffectRecreatedNodeRecord) {
  EmbeddedLoadTracker tracker;
  const auto stale = tracker.begin("same-node", "old-request", 3U, 60U);
  ASSERT_TRUE(stale);
  ASSERT_TRUE(tracker.cancel(*stale));

  const auto current =
      tracker.begin("same-node", "current-request", 4U, 61U);
  ASSERT_TRUE(current);
  ASSERT_NE(*stale, *current);

  EXPECT_FALSE(tracker.consume(*stale, 61U));
  const auto record = tracker.consume(*current, 61U);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->requestId, "current-request");
  EXPECT_EQ(record->connectionId, 4U);
}

TEST(EmbeddedLoadTrackerTest, AcceptsAnEmptyStartupRecoveryOrigin) {
  EmbeddedLoadTracker tracker;

  const auto token = tracker.begin("restored-node", "", 0U, 70U);

  ASSERT_TRUE(token);
  const auto record = tracker.consume(*token, 70U);
  ASSERT_TRUE(record);
  EXPECT_TRUE(record->requestId.empty());
  EXPECT_EQ(record->connectionId, 0U);
}

TEST(EmbeddedLoadTrackerTest, DoesNotWrapAfterIssuingTheMaximumToken) {
  auto tracker = EmbeddedLoadTrackerTestAccess::withTokenAndResource(
      std::numeric_limits<EmbeddedLoadTracker::Token>::max(),
      *std::pmr::get_default_resource());

  const auto maximum = tracker.begin("last-node", "last-request", 1U, 80U);

  ASSERT_TRUE(maximum);
  EXPECT_EQ(*maximum,
            std::numeric_limits<EmbeddedLoadTracker::Token>::max());
  EXPECT_FALSE(tracker.begin("wrapped-node", "wrapped-request", 1U, 80U));
  EXPECT_TRUE(tracker.cancel(*maximum));
  EXPECT_FALSE(tracker.begin("reused-node", "reused-request", 1U, 80U));
}

TEST(EmbeddedLoadTrackerTest, BurnsTokenWhenRecordAllocationFails) {
  ThrowNextAllocationResource resource;
  auto tracker =
      EmbeddedLoadTrackerTestAccess::withTokenAndResource(1U, resource);
  resource.failNextAllocation();

  const auto failed = tracker.begin("node", "request", 2U, 90U);
  const auto recovered = tracker.begin("node", "request", 2U, 90U);

  EXPECT_FALSE(failed);
  ASSERT_TRUE(recovered);
  EXPECT_EQ(*recovered, 2U);
  EXPECT_TRUE(tracker.consume(*recovered, 90U));
}

}  // namespace
