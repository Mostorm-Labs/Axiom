#include "platform/windows/webview2_initial_load_seam.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace {

using canvas::windows::detail::InitialLoadCompletion;
using canvas::windows::detail::InitialLoadState;
using canvas::windows::detail::InitialLoadTracker;
using canvas::windows::detail::NavigationDocumentIdentity;
using canvas::windows::detail::NavigationRequestGenerationTracker;
using canvas::windows::detail::NavigationStartAfterGetterAction;
using canvas::windows::detail::isProvableSameDocumentNavigation;
using canvas::windows::detail::navigationStartAfterGetterAction;
using canvas::windows::detail::nativeNavigationExpectsStarting;

TEST(WebView2InitialLoadTracker,
     ReentrantNavigationStartGettersNeverSilentlyAcceptAStaleEvent) {
  EXPECT_EQ(navigationStartAfterGetterAction(false, false, false, false),
            NavigationStartAfterGetterAction::Ignore);
  EXPECT_EQ(navigationStartAfterGetterAction(true, true, false, false),
            NavigationStartAfterGetterAction::Continue);
  EXPECT_EQ(navigationStartAfterGetterAction(true, false, true, false),
            NavigationStartAfterGetterAction::AbandonCurrentStart);
  EXPECT_EQ(navigationStartAfterGetterAction(true, false, false, true),
            NavigationStartAfterGetterAction::AbandonCurrentStart);

  // A nested request may already have consumed its issued/deferred slot by
  // the time the outer getter returns. The old event still must be cancelled;
  // accepting it would let its completion overwrite the newer document.
  EXPECT_EQ(navigationStartAfterGetterAction(true, false, false, false),
            NavigationStartAfterGetterAction::CancelStaleEvent);
}

TEST(WebView2InitialLoadTracker,
     SameDocumentClassificationRequiresACanonicalIdentityAndAFragment) {
  EXPECT_TRUE(isProvableSameDocumentNavigation(
      {L"https://example.test/page", false},
      {L"https://example.test/page", true}));
  EXPECT_TRUE(isProvableSameDocumentNavigation(
      {L"https://example.test/page", true},
      {L"https://example.test/page", true}));
  EXPECT_TRUE(isProvableSameDocumentNavigation(
      {L"https://example.test/page", true},
      {L"https://example.test/page", false}));

  EXPECT_FALSE(isProvableSameDocumentNavigation(
      {L"https://example.test/page", false},
      {L"https://example.test/page", false}));
  EXPECT_FALSE(isProvableSameDocumentNavigation(
      {L"https://example.test/page", true},
      {L"https://example.test/other", true}));
  EXPECT_FALSE(isProvableSameDocumentNavigation(
      {L"https://example.test/page?version=1", true},
      {L"https://example.test/page?version=2", true}));

  EXPECT_FALSE(nativeNavigationExpectsStarting(
      true, false, {L"https://example.test/page", true},
      {L"https://example.test/page", true}));
  EXPECT_TRUE(nativeNavigationExpectsStarting(
      true, true, {L"https://example.test/page", true},
      {L"https://example.test/page", true}));
  EXPECT_TRUE(nativeNavigationExpectsStarting(
      false, false, {L"https://example.test/page", true},
      {L"https://example.test/page", true}));
}

TEST(WebView2InitialLoadTracker,
     NavigationGenerationRejectsSupersededAndInvalidatedPreflightWork) {
  EXPECT_LT(NavigationRequestGenerationTracker::maximumRequestGeneration(),
            (std::numeric_limits<std::uint64_t>::max)());

  NavigationRequestGenerationTracker generations;
  const auto first = generations.begin();
  ASSERT_TRUE(first);
  EXPECT_TRUE(generations.isCurrent(*first));

  // A rejected or allocation-failed request never calls begin().
  EXPECT_TRUE(generations.isCurrent(*first));

  const auto replacement = generations.begin();
  ASSERT_TRUE(replacement);
  EXPECT_FALSE(generations.isCurrent(*first));
  EXPECT_TRUE(generations.isCurrent(*replacement));

  generations.invalidate();
  EXPECT_FALSE(generations.isCurrent(*replacement));
}

TEST(WebView2InitialLoadTracker,
     CompletesOnlyTheNavigationWhoseNativeStartWasAccepted) {
  InitialLoadTracker tracker;
  std::vector<InitialLoadCompletion> completions;
  ASSERT_TRUE(tracker.setCompletionHandler(
      [&completions](InitialLoadCompletion completion) {
        completions.push_back(completion);
      }));

  ASSERT_TRUE(tracker.request());
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(1U));
  EXPECT_FALSE(tracker.completeSuccessForNavigation(41U));
  ASSERT_TRUE(tracker.acceptNavigationForRequest(41U, 1U));
  EXPECT_TRUE(tracker.completeSuccessForNavigation(41U));
  EXPECT_EQ(tracker.state(), InitialLoadState::Ready);
  ASSERT_EQ(completions.size(), 1U);
  EXPECT_EQ(completions.front().state, InitialLoadState::Ready);
  EXPECT_EQ(completions.front().result, 0);
  EXPECT_FALSE(tracker.completeFailure(-1));
}

TEST(WebView2InitialLoadTracker,
     AReplacementCanRetireTheOldStartBeforeAdmittingTheLatestOne) {
  InitialLoadTracker tracker;
  std::vector<InitialLoadCompletion> completions;
  ASSERT_TRUE(tracker.setCompletionHandler(
      [&completions](InitialLoadCompletion completion) {
        completions.push_back(completion);
      }));

  ASSERT_TRUE(tracker.request());
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(1U));
  ASSERT_TRUE(tracker.cancelPendingNativeNavigation(1U));
  EXPECT_EQ(tracker.state(), InitialLoadState::Pending);
  EXPECT_EQ(tracker.activeNavigationId(), 0U);
  EXPECT_TRUE(completions.empty());

  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(2U));
  ASSERT_TRUE(tracker.acceptNavigationForRequest(42U, 2U));
  EXPECT_TRUE(tracker.completeSuccessForNavigation(42U));
  ASSERT_EQ(completions.size(), 1U);
  EXPECT_EQ(completions.front().state, InitialLoadState::Ready);
}

TEST(WebView2InitialLoadTracker,
     AFailedNativeCallLeavesTheLatestDeferredRequestAdmissible) {
  InitialLoadTracker tracker;
  std::vector<InitialLoadCompletion> completions;
  ASSERT_TRUE(tracker.setCompletionHandler(
      [&completions](InitialLoadCompletion completion) {
        completions.push_back(completion);
      }));

  ASSERT_TRUE(tracker.request());
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(11U));
  // The adapter cancels only the failed call's one slot. A re-entrant latest
  // request can then use it without the old failure becoming terminal.
  ASSERT_TRUE(tracker.cancelPendingNativeNavigation(11U));
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(12U));
  ASSERT_TRUE(tracker.acceptNavigationForRequest(52U, 12U));
  EXPECT_TRUE(tracker.completeSuccessForNavigation(52U));
  EXPECT_TRUE(completions.size() == 1U);
}

TEST(WebView2InitialLoadTracker,
     RedirectDoesNotConsumeTheReplacementNativeRequest) {
  InitialLoadTracker tracker;
  ASSERT_TRUE(tracker.request());
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(21U));
  ASSERT_TRUE(tracker.acceptNavigationForRequest(61U, 21U));

  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(22U));
  EXPECT_EQ(tracker.pendingNativeNavigationStarts(), 1U);
  // Issuing the replacement retires the old active id, so its redirect is
  // stale. Crucially, it does not consume the replacement slot.
  EXPECT_FALSE(tracker.acceptNavigation(61U, true));
  EXPECT_EQ(tracker.pendingNativeNavigationStarts(), 1U);
  ASSERT_TRUE(tracker.acceptNavigationForRequest(62U, 22U));
  EXPECT_EQ(tracker.pendingNativeNavigationStarts(), 0U);
  EXPECT_TRUE(tracker.completeSuccessForNavigation(62U));
}

TEST(WebView2InitialLoadTracker,
     SameDocumentRequestWithoutStartingPreservesActiveCompletion) {
  InitialLoadTracker successful;
  ASSERT_TRUE(successful.request());
  ASSERT_TRUE(successful.tryBeginNativeNavigationForRequest(31U));
  ASSERT_TRUE(successful.acceptNavigationForRequest(71U, 31U));
  ASSERT_TRUE(successful.tryBeginNativeNavigation(false));
  EXPECT_TRUE(successful.completeSuccessForNavigation(71U));
  EXPECT_EQ(successful.state(), InitialLoadState::Ready);

  InitialLoadTracker failed;
  ASSERT_TRUE(failed.request());
  ASSERT_TRUE(failed.tryBeginNativeNavigationForRequest(32U));
  ASSERT_TRUE(failed.acceptNavigationForRequest(72U, 32U));
  ASSERT_TRUE(failed.tryBeginNativeNavigation(false));
  EXPECT_TRUE(failed.completeFailure(-42));
  EXPECT_EQ(failed.state(), InitialLoadState::Failed);
}

TEST(WebView2InitialLoadTracker,
     SameDocumentRequestWithoutStartingDoesNotConsumeTheNextRealStart) {
  InitialLoadTracker tracker;
  ASSERT_TRUE(tracker.request());
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(41U));
  ASSERT_TRUE(tracker.acceptNavigationForRequest(81U, 41U));

  ASSERT_TRUE(tracker.tryBeginNativeNavigation(false));
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(42U));
  EXPECT_TRUE(tracker.acceptNavigationForRequest(82U, 42U));
  EXPECT_EQ(tracker.pendingNativeNavigationStarts(), 0U);
  EXPECT_EQ(tracker.activeNavigationId(), 82U);
}

TEST(WebView2InitialLoadTracker,
     ASecondNativeAdmissionIsRejectedUntilTheFirstIsConsumed) {
  InitialLoadTracker tracker;
  ASSERT_TRUE(tracker.request());
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(51U));
  EXPECT_FALSE(tracker.tryBeginNativeNavigationForRequest(52U));
  EXPECT_EQ(tracker.pendingNativeNavigationStarts(), 1U);
  ASSERT_TRUE(tracker.acceptNavigationForRequest(91U, 51U));
  ASSERT_TRUE(tracker.tryBeginNativeNavigationForRequest(52U));
  EXPECT_TRUE(tracker.acceptNavigationForRequest(92U, 52U));
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
