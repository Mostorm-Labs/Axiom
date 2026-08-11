#pragma once

#include <cstdint>

namespace canvas::platform {

// Main-thread frame request state shared by native hosts. One scheduled
// platform display callback owns one claimed frame. A request which arrives
// while that callback is rendering is retained as the next frame.
class FrameInvalidation {
 public:
  using FrameId = std::uint64_t;

  // Returns true exactly once for a batch of invalidations, allowing a host
  // to ask its platform view for one redraw without a polling render loop.
  bool requestFrame() noexcept {
    const bool shouldSchedule = !displayScheduled_;
    framePending_ = true;
    displayScheduled_ = true;
    return shouldSchedule;
  }

  bool hasPendingFrame() const noexcept { return framePending_; }

  bool isFrameInProgress() const noexcept { return frameInProgress_; }

  // Claim the current platform display callback before making any call which
  // can re-enter the host. A re-entrant callback cannot consume a request
  // made for the next frame while this one is in progress.
  bool beginFrame() noexcept {
    if (!framePending_ || frameInProgress_) return false;

    framePending_ = false;
    displayScheduled_ = false;
    frameInProgress_ = true;
    activeFrameId_ = nextFrameId_++;
    if (nextFrameId_ == 0) ++nextFrameId_;
    return true;
  }

  // Hosts that can re-enter while rendering must retain this id and use the
  // id-taking completion methods. That makes an outer, detached callback a
  // no-op if a new attachment has already claimed a frame.
  FrameId activeFrameId() const noexcept {
    return frameInProgress_ ? activeFrameId_ : 0;
  }

  // A successful commit finishes only the frame it claimed. A request made
  // during rendering remains pending and retains its scheduled display.
  void completeFrame() noexcept { completeFrame(activeFrameId()); }
  void completeFrame(FrameId frameId) noexcept {
    if (!ownsFrame(frameId)) return;
    frameInProgress_ = false;
    activeFrameId_ = 0;
  }

  // A drawable shortage or a failed encoding restores the claimed work, but
  // never schedules from inside the display callback. If another invalidation
  // already scheduled a redraw, displayScheduled_ is deliberately preserved.
  void abandonFrame() noexcept { abandonFrame(activeFrameId()); }
  void abandonFrame(FrameId frameId) noexcept {
    if (!ownsFrame(frameId)) return;
    framePending_ = true;
    frameInProgress_ = false;
    activeFrameId_ = 0;
  }

  void failFrame() noexcept { failFrame(activeFrameId()); }
  void failFrame(FrameId frameId) noexcept { abandonFrame(frameId); }

  // Drop host-local work when its native view is detached. The monotonically
  // increasing id is intentionally not reset: an outer callback from the old
  // attachment must not match a later claimed frame.
  void reset() noexcept {
    framePending_ = false;
    displayScheduled_ = false;
    frameInProgress_ = false;
    activeFrameId_ = 0;
  }

 private:
  bool ownsFrame(FrameId frameId) const noexcept {
    return frameId != 0 && frameInProgress_ && frameId == activeFrameId_;
  }

  bool framePending_ = false;
  bool displayScheduled_ = false;
  bool frameInProgress_ = false;
  FrameId activeFrameId_ = 0;
  FrameId nextFrameId_ = 1;
};

}  // namespace canvas::platform
