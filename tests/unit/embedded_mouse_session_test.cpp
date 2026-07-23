#include "platform/windows/embedded_mouse_session.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using canvas::windows::EmbeddedMouseButton;
using canvas::windows::EmbeddedMouseButtons;
using canvas::windows::EmbeddedMouseCancellationSink;
using canvas::windows::EmbeddedMouseSession;
using canvas::windows::embeddedMouseButtonMask;
using canvas::windows::runEmbeddedMouseCancellation;

class RecordingCancellationSink final
    : public EmbeddedMouseCancellationSink {
 public:
  HRESULT sendLeave() noexcept override {
    hovered = false;
    calls.emplace_back("leave");
    return leaveResult;
  }

  HRESULT sendButtonUp(EmbeddedMouseButton button,
                       EmbeddedMouseButtons) noexcept override {
    calls.emplace_back("up:" + std::to_string(
                                  embeddedMouseButtonMask(button)));
    // The production sink uses a surface-local outside point. This fake models
    // the DOM click precondition and proves the cancellation order defeats it.
    if (hovered) ++clicks;
    if (button == failingButton) return E_ABORT;
    return S_OK;
  }

  std::vector<std::string> calls;
  bool hovered = true;
  int clicks = 0;
  HRESULT leaveResult = S_OK;
  EmbeddedMouseButton failingButton =
      static_cast<EmbeddedMouseButton>(0);
};

TEST(EmbeddedMouseSession, InterleavedButtonsReleaseCaptureOnlyAfterLastUp) {
  EmbeddedMouseSession session;

  const auto leftDown = session.buttonDown(EmbeddedMouseButton::Left, true);
  EXPECT_TRUE(leftDown.capture);
  EXPECT_TRUE(leftDown.startTrackingLeave);
  const auto rightDown = session.buttonDown(EmbeddedMouseButton::Right, true);
  EXPECT_FALSE(rightDown.capture);

  const auto leftUp = session.buttonUp(EmbeddedMouseButton::Left, true);
  EXPECT_FALSE(leftUp.releaseCapture);
  EXPECT_TRUE(session.buttons() != 0);
  const auto rightUp = session.buttonUp(EmbeddedMouseButton::Right, true);
  EXPECT_TRUE(rightUp.releaseCapture);
  EXPECT_EQ(session.buttons(), 0);
}

TEST(EmbeddedMouseSession, ExpectedCaptureChangeAfterLastUpPreservesHover) {
  EmbeddedMouseSession session;
  session.buttonDown(EmbeddedMouseButton::Left, true);
  const auto up = session.buttonUp(EmbeddedMouseButton::Left, true);
  ASSERT_TRUE(up.releaseCapture);
  ASSERT_TRUE(session.hovered());

  const auto captureChanged = session.captureLost();
  EXPECT_FALSE(captureChanged.handled());
  EXPECT_FALSE(captureChanged.sendLeave);
  EXPECT_TRUE(session.hovered());
}

TEST(EmbeddedMouseSession, DuplicateDownAndStrayUpDoNotCorruptMask) {
  EmbeddedMouseSession session;
  const auto first = session.buttonDown(EmbeddedMouseButton::Left, true);
  const auto duplicate = session.buttonDown(EmbeddedMouseButton::Left, true);
  EXPECT_TRUE(first.capture);
  EXPECT_FALSE(duplicate.capture);
  EXPECT_EQ(session.buttons(),
            embeddedMouseButtonMask(EmbeddedMouseButton::Left));

  const auto stray = session.buttonUp(EmbeddedMouseButton::Right, true);
  EXPECT_FALSE(stray.forward);
  EXPECT_FALSE(stray.releaseCapture);
  EXPECT_EQ(session.buttons(),
            embeddedMouseButtonMask(EmbeddedMouseButton::Left));
  EXPECT_TRUE(session.buttonUp(EmbeddedMouseButton::Left, true).releaseCapture);
}

TEST(EmbeddedMouseSession, CaptureLossCancelsEveryButtonWithoutClick) {
  EmbeddedMouseSession session;
  session.buttonDown(EmbeddedMouseButton::Left, true);
  session.buttonDown(EmbeddedMouseButton::X1, true);
  session.buttonDown(EmbeddedMouseButton::X2, true);

  const auto cancel = session.captureLost();
  EXPECT_TRUE(cancel.sendLeave);
  EXPECT_FALSE(cancel.releaseCapture);
  EXPECT_TRUE(canvas::windows::hasEmbeddedMouseButton(
      cancel.cancelButtons, EmbeddedMouseButton::Left));
  EXPECT_TRUE(canvas::windows::hasEmbeddedMouseButton(
      cancel.cancelButtons, EmbeddedMouseButton::X1));
  EXPECT_TRUE(canvas::windows::hasEmbeddedMouseButton(
      cancel.cancelButtons, EmbeddedMouseButton::X2));
  EXPECT_EQ(session.buttons(), 0);
  EXPECT_FALSE(session.hovered());

  RecordingCancellationSink sink;
  EXPECT_EQ(runEmbeddedMouseCancellation(cancel.cancelButtons, sink), S_OK);
  EXPECT_EQ(sink.calls,
            (std::vector<std::string>{"leave", "up:1", "up:8", "up:16"}));
  EXPECT_EQ(sink.clicks, 0);

  const auto nextDown = session.buttonDown(EmbeddedMouseButton::Left, true);
  const auto nextUp = session.buttonUp(EmbeddedMouseButton::Left, true);
  EXPECT_TRUE(nextDown.capture);
  EXPECT_TRUE(nextUp.forward);
  EXPECT_TRUE(nextUp.releaseCapture);
}

TEST(EmbeddedMouseSession, DisableRequestsReleaseForAnOwnedCapture) {
  EmbeddedMouseSession session;
  session.buttonDown(EmbeddedMouseButton::Middle, true);

  const auto disable = session.disable();
  EXPECT_TRUE(disable.releaseCapture);
  EXPECT_TRUE(disable.sendLeave);
  EXPECT_EQ(disable.cancelButtons,
            embeddedMouseButtonMask(EmbeddedMouseButton::Middle));
  EXPECT_EQ(session.buttons(), 0);
}

TEST(EmbeddedMouseSession, HoverTransitionsAndDisableSendOneLeaveEach) {
  EmbeddedMouseSession session;
  int enters = 0;
  int leaves = 0;
  const auto count = [&enters, &leaves](const auto& decision) {
    if (decision.startTrackingLeave) ++enters;
    if (decision.sendLeave) ++leaves;
  };

  count(session.move(true));
  count(session.move(true));
  count(session.move(false));
  count(session.move(false));
  EXPECT_EQ(enters, 1);
  EXPECT_EQ(leaves, 1);

  count(session.move(true));
  count(session.nativeLeave());
  count(session.nativeLeave());
  EXPECT_EQ(enters, 2);
  EXPECT_EQ(leaves, 2);

  count(session.move(true));
  count(session.disable());
  count(session.disable());
  EXPECT_EQ(enters, 3);
  EXPECT_EQ(leaves, 3);
}

TEST(EmbeddedMouseSession, CancellationRunsAllUpsAndReturnsFirstFailure) {
  constexpr EmbeddedMouseButtons buttons =
      embeddedMouseButtonMask(EmbeddedMouseButton::Left) |
      embeddedMouseButtonMask(EmbeddedMouseButton::Middle) |
      embeddedMouseButtonMask(EmbeddedMouseButton::X2);
  RecordingCancellationSink sink;
  sink.failingButton = EmbeddedMouseButton::Middle;

  EXPECT_EQ(runEmbeddedMouseCancellation(buttons, sink), E_ABORT);
  EXPECT_EQ(sink.calls,
            (std::vector<std::string>{"leave", "up:1", "up:4", "up:16"}));
  EXPECT_EQ(sink.clicks, 0);
}

}  // namespace
