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

TEST(WebView2InitialLoadTracker,
     SupersedeRetiresTheOldNavigationBeforeTheReplacementStarts) {
  InitialLoadTracker tracker;
  std::vector<InitialLoadCompletion> completions;
  ASSERT_TRUE(tracker.setCompletionHandler(
      [&completions](InitialLoadCompletion completion) {
        completions.push_back(completion);
      }));

  ASSERT_TRUE(tracker.request());
  tracker.acceptNavigation(41U);

  // WebView2 can synchronously complete navigation 41 as cancelled from
  // inside Navigate() before it raises NavigationStarting for navigation 42.
  // Once the replacement request is ready to enter WebView2, 41 must no
  // longer be allowed to terminate the one-shot initial-load lifecycle.
  tracker.retireActiveNavigation();
  const auto completeFailureForNavigation =
      [&tracker](std::uint64_t navigationId, std::int32_t result) {
        return tracker.activeNavigationId() == navigationId &&
               tracker.completeFailure(result);
      };
  EXPECT_FALSE(completeFailureForNavigation(41U, -42));

  EXPECT_EQ(tracker.state(), InitialLoadState::Pending);
  EXPECT_TRUE(completions.empty());

  tracker.acceptNavigation(42U);
  EXPECT_TRUE(tracker.completeSuccessForNavigation(42U));
  EXPECT_EQ(tracker.state(), InitialLoadState::Ready);
  ASSERT_EQ(completions.size(), 1U);
  EXPECT_EQ(completions.front().state, InitialLoadState::Ready);
  EXPECT_EQ(completions.front().result, 0);
}

TEST(WebView2InitialLoadTracker,
     RequestRejectedBeforeWebViewLeavesTheCurrentNavigationActive) {
  InitialLoadTracker tracker;
  ASSERT_TRUE(tracker.request());
  tracker.acceptNavigation(41U);

  // navigate() records every request before validation. An invalid, denied,
  // or allocation-failed replacement stops without entering WebView2 and
  // therefore must not retire navigation 41.
  tracker.noteNavigationRequest();
  EXPECT_EQ(tracker.activeNavigationId(), 41U);
  EXPECT_TRUE(tracker.completeSuccessForNavigation(41U));
  EXPECT_EQ(tracker.state(), InitialLoadState::Ready);
}

TEST(WebView2InitialLoadTracker,
     SynchronousNavigateFailureStillTerminatesAfterRetirement) {
  InitialLoadTracker tracker;
  std::vector<InitialLoadCompletion> completions;
  ASSERT_TRUE(tracker.setCompletionHandler(
      [&completions](InitialLoadCompletion completion) {
        completions.push_back(completion);
      }));

  ASSERT_TRUE(tracker.request());
  tracker.acceptNavigation(41U);
  tracker.retireActiveNavigation();

  EXPECT_TRUE(tracker.completeFailure(-43));
  EXPECT_EQ(tracker.state(), InitialLoadState::Failed);
  ASSERT_EQ(completions.size(), 1U);
  EXPECT_EQ(completions.front().state, InitialLoadState::Failed);
  EXPECT_EQ(completions.front().result, -43);
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
