#include "platform/windows/webview2_message_log.h"

#include <gtest/gtest.h>

#include <string>

TEST(WebView2MessageLog, DropsOldestMessagesAtTheCountBound) {
  canvas::windows::detail::WebView2MessageLog log;
  for (std::size_t index = 0; index < 300; ++index) {
    log.push(std::to_wstring(index));
  }

  EXPECT_LE(log.values().size(),
            canvas::windows::detail::WebView2MessageLog::maxSize());
  ASSERT_FALSE(log.values().empty());
  EXPECT_EQ(log.values().back(), L"299");
  EXPECT_NE(log.values().front(), L"0");
}

TEST(WebView2MessageLog, RejectsAnOversizedSingleMessageBeforeCopying) {
  canvas::windows::detail::WebView2MessageLog log;
  const std::wstring oversized(
      canvas::windows::detail::WebView2MessageLog::maxMessageCodeUnits() + 1U,
      L'x');

  EXPECT_EQ(log.tryPush(oversized),
            canvas::windows::detail::MessagePushResult::Oversized);
  EXPECT_TRUE(log.values().empty());
  EXPECT_EQ(log.totalCodeUnits(), 0U);
}

TEST(WebView2MessageLog, EvictsOldestMessagesToStayWithinCumulativeLimit) {
  canvas::windows::detail::WebView2MessageLog log;
  const std::wstring chunk(
      canvas::windows::detail::WebView2MessageLog::maxMessageCodeUnits(),
      L'x');

  const std::size_t chunks =
      canvas::windows::detail::WebView2MessageLog::maxTotalCodeUnits() /
      chunk.size();
  for (std::size_t index = 0; index < chunks; ++index) {
    EXPECT_EQ(log.tryPush(chunk),
              canvas::windows::detail::MessagePushResult::Added);
  }
  EXPECT_EQ(log.totalCodeUnits(),
            canvas::windows::detail::WebView2MessageLog::maxTotalCodeUnits());

  const std::wstring newest(L"newest");
  EXPECT_EQ(log.tryPush(newest),
            canvas::windows::detail::MessagePushResult::Added);
  EXPECT_LT(log.totalCodeUnits(),
            canvas::windows::detail::WebView2MessageLog::maxTotalCodeUnits());
  ASSERT_FALSE(log.values().empty());
  EXPECT_EQ(log.values().back(), newest);
  std::size_t retainedCodeUnits = 0U;
  for (const auto& value : log.values()) retainedCodeUnits += value.size();
  EXPECT_EQ(log.totalCodeUnits(), retainedCodeUnits);
}

TEST(WebView2PendingMessageQueue, HasTheSmallerCountBoundAndSameByteBudget) {
  canvas::windows::detail::WebView2PendingMessageQueue queue;
  EXPECT_EQ(queue.maxSize(), 64U);
  EXPECT_EQ(queue.maxTotalCodeUnits(), 4U * 1024U * 1024U);
  for (std::size_t index = 0; index < queue.maxSize() + 10U; ++index) {
    EXPECT_EQ(queue.tryPush(std::to_wstring(index)),
              canvas::windows::detail::MessagePushResult::Added);
  }
  EXPECT_LE(queue.values().size(), queue.maxSize());
  EXPECT_LE(queue.totalCodeUnits(), queue.maxTotalCodeUnits());
  std::size_t retainedCodeUnits = 0U;
  for (const auto& value : queue.values()) retainedCodeUnits += value.size();
  EXPECT_EQ(queue.totalCodeUnits(), retainedCodeUnits);
}
