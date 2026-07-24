#include "canvas/app/whiteboard_lifecycle.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using canvas::app::WhiteboardLifecycleOperations;
using canvas::app::finishWhiteboardMessageLoop;
using canvas::app::stopAndClearWhiteboardIpc;

class FakeLifecycleOperations final : public WhiteboardLifecycleOperations {
 public:
  std::uint32_t captureMessageError() noexcept override {
    calls.emplace_back("capture-message-error");
    return ambientError;
  }

  bool isWindowAlive() noexcept override {
    calls.emplace_back("is-window-alive");
    return windowAlive;
  }

  void destroyWindow() noexcept override {
    calls.emplace_back("destroy-window");
  }

  void stopAndJoinIpcServer() noexcept override {
    calls.emplace_back("stop-and-join");
    serverJoined = true;
    if (overwriteErrorDuringStop) ambientError = 31;
  }

  void clearIpcCallbackQueue() noexcept override {
    calls.emplace_back("clear-callback-queue");
    callbackStateTouchedBeforeJoin = !serverJoined;
  }

  void clearWindowHandle() noexcept override {
    calls.emplace_back("clear-window-handle");
    windowHandleCleared = true;
  }

  std::vector<std::string> calls;
  std::uint32_t ambientError = 0;
  bool windowAlive = false;
  bool serverJoined = false;
  bool callbackStateTouchedBeforeJoin = false;
  bool overwriteErrorDuringStop = false;
  bool windowHandleCleared = false;
};

TEST(WhiteboardLifecycleTest,
     StopsAndJoinsServerBeforeTouchingCallbackState) {
  FakeLifecycleOperations operations;

  stopAndClearWhiteboardIpc(operations);
  stopAndClearWhiteboardIpc(operations);

  EXPECT_FALSE(operations.callbackStateTouchedBeforeJoin);
  EXPECT_EQ(operations.calls, (std::vector<std::string>{
                                  "stop-and-join", "clear-callback-queue",
                                  "stop-and-join", "clear-callback-queue"}));
}

TEST(WhiteboardLifecycleTest,
     WmQuitWithoutDestroyStillCleansLiveWindowAndIpc) {
  FakeLifecycleOperations operations;
  operations.ambientError = 123;
  operations.windowAlive = true;

  const auto exit = finishWhiteboardMessageLoop(0, 37, operations);

  EXPECT_FALSE(exit.failed);
  EXPECT_EQ(exit.quitCode, 37);
  EXPECT_EQ(exit.errorCode, 0U);
  EXPECT_TRUE(operations.windowHandleCleared);
  EXPECT_EQ(operations.calls, (std::vector<std::string>{
                                  "is-window-alive", "destroy-window",
                                  "stop-and-join", "clear-callback-queue",
                                  "clear-window-handle"}));
}

TEST(WhiteboardLifecycleTest,
     GetMessageFailureCapturesErrorBeforeCleanupAndStillStopsIpc) {
  FakeLifecycleOperations operations;
  operations.ambientError = 5;
  operations.overwriteErrorDuringStop = true;

  const auto exit = finishWhiteboardMessageLoop(-1, 0, operations);

  EXPECT_TRUE(exit.failed);
  EXPECT_EQ(exit.errorCode, 5U);
  EXPECT_TRUE(operations.windowHandleCleared);
  EXPECT_EQ(operations.ambientError, 31U);
  EXPECT_EQ(operations.calls,
            (std::vector<std::string>{
                "capture-message-error", "is-window-alive", "stop-and-join",
                "clear-callback-queue", "clear-window-handle"}));
}

}  // namespace
