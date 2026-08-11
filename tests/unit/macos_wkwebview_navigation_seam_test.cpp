#include "platform/macos/wkwebview_navigation_seam.h"

#include <gtest/gtest.h>

namespace {
using canvas::macos::detail::InitialLoadState;
using canvas::macos::detail::NavigationClass;
using canvas::macos::detail::NavigationEventAction;
using canvas::macos::detail::NavigationPolicyOptions;
using canvas::macos::detail::NavigationTracker;

TEST(MacosWKWebViewNavigationSeam, PolicyIsHttpsPackageRootOrBoundedOptInData) {
  NavigationPolicyOptions options{"/Applications/Canvas.app/Contents/Resources/web", false};
  EXPECT_EQ(canvas::macos::detail::classifyNavigation("https://example.test/a", options), NavigationClass::Https);
  EXPECT_EQ(canvas::macos::detail::classifyNavigation("http://example.test/a", options), NavigationClass::Denied);
  EXPECT_EQ(canvas::macos::detail::classifyNavigation("file:///Applications/Canvas.app/Contents/Resources/web/index.html", options), NavigationClass::PackagedFile);
  EXPECT_EQ(canvas::macos::detail::classifyNavigation("file:///Applications/Canvas.app/Contents/Resources/web-extra/x", options), NavigationClass::Denied);
  EXPECT_EQ(canvas::macos::detail::classifyNavigation("file:///Applications/Canvas.app/Contents/Resources/web/../secret", options), NavigationClass::Denied);
  EXPECT_EQ(canvas::macos::detail::classifyNavigation("data:text/html,<p>ok</p>", options), NavigationClass::Denied);
  options.allowTestDataUrls = true;
  EXPECT_EQ(canvas::macos::detail::classifyNavigation("data:text/html,<p>ok</p>", options), NavigationClass::TestData);
  EXPECT_EQ(canvas::macos::detail::classifyNavigation("data:text/html;base64,xxx", options), NavigationClass::TestData);
}

TEST(MacosWKWebViewNavigationSeam, TrackerLatestWinsAndDeliversOneTerminalEvent) {
  NavigationTracker tracker;
  const auto first = tracker.submit();
  ASSERT_TRUE(first);
  const auto issued = tracker.issueNext();
  ASSERT_EQ(issued, first);
  const auto replacement = tracker.submit();
  ASSERT_TRUE(replacement);
  EXPECT_EQ(tracker.complete(*issued, true), NavigationEventAction::PromoteNext);
  const auto latest = tracker.issueNext();
  ASSERT_EQ(latest, replacement);
  EXPECT_EQ(tracker.complete(*latest, false), NavigationEventAction::DeliverFailed);
  EXPECT_EQ(tracker.state(), InitialLoadState::Failed);
  EXPECT_EQ(tracker.complete(*latest, true), NavigationEventAction::Ignore);
}

TEST(MacosWKWebViewNavigationSeam,
     ReentrantStaleLaunchRetiresTheIssuedGenerationBeforePumpingLatest) {
  NavigationTracker tracker;
  const auto issued = tracker.submit();
  ASSERT_TRUE(issued);
  ASSERT_EQ(tracker.issueNext(), issued);

  // Model navigate(B) re-entering while loadRequest(A) is still on the stack.
  const auto latest = tracker.submit();
  ASSERT_TRUE(latest);
  EXPECT_TRUE(tracker.isActive(*issued));

  // The stale loadRequest(A) return must terminally retire A even if WebKit
  // never sends a completion callback for the superseded launch.
  EXPECT_EQ(tracker.complete(*issued, false), NavigationEventAction::PromoteNext);
  EXPECT_FALSE(tracker.isActive(*issued));
  EXPECT_EQ(tracker.issueNext(), latest);
  EXPECT_TRUE(tracker.isActive(*latest));
  EXPECT_EQ(tracker.complete(*latest, true), NavigationEventAction::DeliverReady);
  EXPECT_EQ(tracker.state(), InitialLoadState::Ready);
}

TEST(MacosWKWebViewNavigationSeam,
     TerminalRecordPreservesUriUntilLateCompletionHandlerRegistration) {
  canvas::macos::detail::InitialLoadTerminalRecord record;
  record.record(InitialLoadState::Ready, "data:text/html,latest");
  record.record(InitialLoadState::Failed, "https://stale.test");
  ASSERT_TRUE(record.recorded());
  EXPECT_EQ(record.state(), InitialLoadState::Ready);
  EXPECT_EQ(record.uri(), "data:text/html,latest");
}

}  // namespace
