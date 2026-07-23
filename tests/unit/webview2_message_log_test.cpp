#include "platform/windows/webview2_message_log.h"

#include <gtest/gtest.h>

#include <string>

TEST(WebView2MessageLog, DropsOldestMessagesInBoundedBatches) {
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
