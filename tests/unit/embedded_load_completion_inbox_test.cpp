#include "canvas/app/embedded_load_completion_inbox.h"

#include <gtest/gtest.h>

namespace {
using Inbox = canvas::app::EmbeddedLoadCompletionInbox;
using Event = Inbox::Event;

Event ready(std::uint64_t token, std::uint64_t generation = 1U) {
  return {token, generation, Inbox::Outcome::Ready, 0};
}
Event failed(std::uint64_t token, std::uint64_t generation = 1U) {
  return {token, generation, Inbox::Outcome::Failed, -1};
}

TEST(EmbeddedLoadCompletionInboxTest, InvalidEventsAreRejected) {
  Inbox inbox;
  EXPECT_EQ(inbox.enqueue(ready(0U)).status, Inbox::EnqueueStatus::Invalid);
  EXPECT_EQ(inbox.enqueue(ready(1U, 0U)).status, Inbox::EnqueueStatus::Invalid);
  EXPECT_EQ(inbox.enqueue({2U, 1U, Inbox::Outcome::Ready, -1}).status,
            Inbox::EnqueueStatus::Invalid);
  EXPECT_EQ(inbox.enqueue({3U, 1U, Inbox::Outcome::Failed, 0}).status,
            Inbox::EnqueueStatus::Invalid);
}

TEST(EmbeddedLoadCompletionInboxTest, FifoAndGenerationCancellation) {
  Inbox inbox;
  ASSERT_EQ(inbox.enqueue(ready(1U, 7U)).status, Inbox::EnqueueStatus::Accepted);
  ASSERT_EQ(inbox.enqueue(failed(2U, 8U)).status, Inbox::EnqueueStatus::Accepted);
  ASSERT_EQ(inbox.enqueue(ready(3U, 7U)).status, Inbox::EnqueueStatus::Accepted);
  EXPECT_EQ(inbox.cancelGeneration(7U), 2U);
  Event event;
  ASSERT_TRUE(inbox.pop(event));
  EXPECT_EQ(event.token, 2U);
  EXPECT_EQ(event.outcome, Inbox::Outcome::Failed);
  EXPECT_FALSE(inbox.pop(event));
}

TEST(EmbeddedLoadCompletionInboxTest, NotificationCoalescesAndRecoversPostFailure) {
  Inbox inbox;
  EXPECT_TRUE(inbox.enqueue(ready(1U)).shouldPostNotification);
  EXPECT_FALSE(inbox.enqueue(ready(2U)).shouldPostNotification);
  EXPECT_TRUE(inbox.notificationPostFailed());
  EXPECT_TRUE(inbox.requestNotificationIfNeeded());
  EXPECT_TRUE(inbox.consumeNotification());
  Event event;
  ASSERT_TRUE(inbox.pop(event));
  EXPECT_EQ(event.token, 1U);
}
}  // namespace
