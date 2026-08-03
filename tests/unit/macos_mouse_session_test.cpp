#include "platform/macos/macos_mouse_session.h"

#include <gtest/gtest.h>

namespace canvas::macos {
namespace {

RawMacMouseEvent mouseEvent(MacMousePhase phase, float x = 1.0F,
                            std::int64_t buttonNumber = 0) {
  RawMacMouseEvent raw;
  raw.localPosition = {x, 2.0F};
  raw.boundsSize = {100.0F, 100.0F};
  raw.viewFlipped = true;
  raw.timestampSeconds = static_cast<double>(x);
  raw.buttonNumber = buttonNumber;
  raw.phase = phase;
  return raw;
}

TEST(MacosMouseSessionTest, ReusesIdUntilUpAndAllocatesNewIdNextStroke) {
  MacosMouseSession session;

  const auto down = session.consume(mouseEvent(MacMousePhase::Down, 1.0F));
  const auto move = session.consume(mouseEvent(MacMousePhase::Move, 2.0F));
  const auto up = session.consume(mouseEvent(MacMousePhase::Up, 3.0F));
  const auto nextDown =
      session.consume(mouseEvent(MacMousePhase::Down, 4.0F));

  ASSERT_EQ(down.size(), 1U);
  ASSERT_EQ(move.size(), 1U);
  ASSERT_EQ(up.size(), 1U);
  ASSERT_EQ(nextDown.size(), 1U);
  EXPECT_NE(down[0].pointerId, 0U);
  EXPECT_EQ(move[0].pointerId, down[0].pointerId);
  EXPECT_EQ(up[0].pointerId, down[0].pointerId);
  EXPECT_GT(nextDown[0].pointerId, down[0].pointerId);
  EXPECT_TRUE(session.active());
}

TEST(MacosMouseSessionTest, IgnoresOrphanMoveAndUp) {
  MacosMouseSession session;

  EXPECT_TRUE(session.consume(mouseEvent(MacMousePhase::Move)).empty());
  EXPECT_TRUE(session.consume(mouseEvent(MacMousePhase::Up)).empty());
  EXPECT_FALSE(session.active());
}

TEST(MacosMouseSessionTest, LifecycleCancelUsesLastSampleExactlyOnce) {
  MacosMouseSession session;
  const auto down = session.consume(mouseEvent(MacMousePhase::Down, 1.0F));
  const auto move = session.consume(mouseEvent(MacMousePhase::Move, 8.0F));

  const auto firstCancel = session.cancel();
  const auto secondCancel = session.cancel();

  ASSERT_EQ(down.size(), 1U);
  ASSERT_EQ(move.size(), 1U);
  ASSERT_TRUE(firstCancel.has_value());
  EXPECT_EQ(firstCancel->pointerId, down[0].pointerId);
  EXPECT_EQ(firstCancel->screenPosition, move[0].screenPosition);
  EXPECT_EQ(firstCancel->timestampMicros, move[0].timestampMicros);
  EXPECT_EQ(firstCancel->phase, input::PointerPhase::Cancel);
  EXPECT_FALSE(secondCancel.has_value());
  EXPECT_FALSE(session.active());
}

TEST(MacosMouseSessionTest, EventAndDeviceChangesDoNotChangeSessionId) {
  MacosMouseSession session;
  RawMacMouseEvent downRaw = mouseEvent(MacMousePhase::Down);
  downRaw.eventNumber = 10;
  downRaw.deviceId = 20;
  RawMacMouseEvent moveRaw = mouseEvent(MacMousePhase::Move);
  moveRaw.eventNumber = 99;
  moveRaw.deviceId = 88;

  const auto down = session.consume(downRaw);
  const auto move = session.consume(moveRaw);

  ASSERT_EQ(down.size(), 1U);
  ASSERT_EQ(move.size(), 1U);
  EXPECT_EQ(move[0].pointerId, down[0].pointerId);
}

TEST(MacosMouseSessionTest, DuplicateDownCancelsOldThenStartsNewSession) {
  MacosMouseSession session;
  const auto first = session.consume(mouseEvent(MacMousePhase::Down, 1.0F));
  const auto duplicate =
      session.consume(mouseEvent(MacMousePhase::Down, 2.0F));
  const auto up = session.consume(mouseEvent(MacMousePhase::Up, 3.0F));

  ASSERT_EQ(first.size(), 1U);
  ASSERT_EQ(duplicate.size(), 2U);
  ASSERT_EQ(up.size(), 1U);
  EXPECT_EQ(duplicate[0].phase, input::PointerPhase::Cancel);
  EXPECT_EQ(duplicate[0].pointerId, first[0].pointerId);
  EXPECT_EQ(duplicate[1].phase, input::PointerPhase::Down);
  EXPECT_GT(duplicate[1].pointerId, first[0].pointerId);
  EXPECT_EQ(up[0].pointerId, duplicate[1].pointerId);
  EXPECT_FALSE(session.active());
}

TEST(MacosMouseSessionTest, IgnoresNonLeftMouseEventsWithoutEndingActiveStroke) {
  MacosMouseSession session;
  const auto down = session.consume(mouseEvent(MacMousePhase::Down));

  EXPECT_TRUE(session.consume(mouseEvent(MacMousePhase::Move, 2.0F, 1)).empty());
  EXPECT_TRUE(session.consume(mouseEvent(MacMousePhase::Up, 3.0F, 1)).empty());
  EXPECT_TRUE(session.active());

  const auto leftUp = session.consume(mouseEvent(MacMousePhase::Up, 4.0F));
  ASSERT_EQ(down.size(), 1U);
  ASSERT_EQ(leftUp.size(), 1U);
  EXPECT_EQ(leftUp[0].pointerId, down[0].pointerId);
}

TEST(MacosMouseSessionTest, RawCancelEndsActiveSession) {
  MacosMouseSession session;
  const auto down = session.consume(mouseEvent(MacMousePhase::Down));
  const auto cancelled =
      session.consume(mouseEvent(MacMousePhase::Cancel, 2.0F, 1));

  ASSERT_EQ(down.size(), 1U);
  ASSERT_EQ(cancelled.size(), 1U);
  EXPECT_EQ(cancelled[0].pointerId, down[0].pointerId);
  EXPECT_EQ(cancelled[0].phase, input::PointerPhase::Cancel);
  EXPECT_FALSE(session.active());
  EXPECT_FALSE(session.cancel().has_value());
}

}  // namespace
}  // namespace canvas::macos
