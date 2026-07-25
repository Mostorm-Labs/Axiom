#include "platform/windows/webview2_initial_load_seam.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using canvas::windows::detail::InitialLoadCompletion;
using canvas::windows::detail::InitialLoadState;
using canvas::windows::detail::InitialLoadTracker;

TEST(WebView2InitialLoadTracker,
     CompletesOnlyTheLatestAcceptedNavigationExactlyOnce) {
  InitialLoadTracker tracker;
  std::vector<InitialLoadCompletion> completions;
  ASSERT_TRUE(tracker.setCompletionHandler(
      [&completions](InitialLoadCompletion completion) {
        completions.push_back(completion);
      }));

  EXPECT_TRUE(tracker.request());
  EXPECT_EQ(tracker.state(), InitialLoadState::Pending);
  tracker.acceptNavigation(41U);
  tracker.acceptNavigation(42U);

  EXPECT_FALSE(tracker.completeSuccessForNavigation(41U));
  EXPECT_TRUE(completions.empty());
  EXPECT_TRUE(tracker.completeSuccessForNavigation(42U));
  EXPECT_EQ(tracker.state(), InitialLoadState::Ready);
  ASSERT_EQ(completions.size(), 1U);
  EXPECT_EQ(completions.front().state, InitialLoadState::Ready);
  EXPECT_EQ(completions.front().result, 0);

  EXPECT_FALSE(tracker.completeFailure(-1));
  EXPECT_EQ(completions.size(), 1U);
}

TEST(WebView2InitialLoadTracker, FailureMovesOutHandlerAndSwallowsExceptions) {
  InitialLoadTracker tracker;
  int calls = 0;
  ASSERT_TRUE(tracker.setCompletionHandler(
      [&calls](InitialLoadCompletion) {
        ++calls;
        throw 1;
      }));

  ASSERT_TRUE(tracker.request());
  EXPECT_TRUE(tracker.completeFailure(-42));
  EXPECT_EQ(tracker.state(), InitialLoadState::Failed);
  EXPECT_EQ(tracker.completion().result, -42);
  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(tracker.completeFailure(-43));
  EXPECT_EQ(calls, 1);
}

TEST(WebView2InitialLoadTracker,
     HandlerMustBeSetBeforeTheFirstNavigationRequestAndCancelDoesNotFail) {
  InitialLoadTracker tracker;
  EXPECT_TRUE(tracker.setCompletionHandler([](InitialLoadCompletion) {}));
  EXPECT_FALSE(tracker.setCompletionHandler([](InitialLoadCompletion) {}));
  tracker.noteNavigationRequest();
  EXPECT_FALSE(tracker.setCompletionHandler([](InitialLoadCompletion) {}));
  ASSERT_TRUE(tracker.request());
  tracker.cancel();
  EXPECT_EQ(tracker.state(), InitialLoadState::Pending);
  EXPECT_EQ(tracker.completion().state, InitialLoadState::Pending);
}

}  // namespace
