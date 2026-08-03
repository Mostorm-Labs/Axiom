#pragma once

#include <cstdint>

namespace canvas::app {

class WhiteboardLifecycleOperations {
 public:
  virtual ~WhiteboardLifecycleOperations() = default;

  virtual std::uint32_t captureMessageError() noexcept = 0;
  virtual bool isWindowAlive() noexcept = 0;
  virtual void destroyWindow() noexcept = 0;
  virtual void stopAndJoinIpcServer() noexcept = 0;
  virtual void clearIpcCallbackQueue() noexcept = 0;
  virtual void clearWindowHandle() noexcept = 0;
};

struct MessageLoopExit {
  bool failed = false;
  std::intptr_t quitCode = 0;
  std::uint32_t errorCode = 0;
};

inline void stopAndClearWhiteboardIpc(
    WhiteboardLifecycleOperations& operations) noexcept {
  operations.stopAndJoinIpcServer();
  operations.clearIpcCallbackQueue();
}

inline MessageLoopExit finishWhiteboardMessageLoop(
    int messageResult, std::intptr_t quitCode,
    WhiteboardLifecycleOperations& operations) noexcept {
  const std::uint32_t messageError =
      messageResult < 0 ? operations.captureMessageError() : 0U;
  if (operations.isWindowAlive()) {
    operations.destroyWindow();
  }
  stopAndClearWhiteboardIpc(operations);
  operations.clearWindowHandle();
  return MessageLoopExit{messageResult < 0, quitCode, messageError};
}

}  // namespace canvas::app
