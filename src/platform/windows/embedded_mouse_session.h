#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

namespace canvas::windows {

enum class EmbeddedMouseButton : std::uint8_t {
  Left = 1U << 0U,
  Right = 1U << 1U,
  Middle = 1U << 2U,
  X1 = 1U << 3U,
  X2 = 1U << 4U,
};

using EmbeddedMouseButtons = std::uint8_t;

constexpr EmbeddedMouseButtons embeddedMouseButtonMask(
    EmbeddedMouseButton button) noexcept {
  return static_cast<EmbeddedMouseButtons>(button);
}

constexpr bool hasEmbeddedMouseButton(EmbeddedMouseButtons buttons,
                                      EmbeddedMouseButton button) noexcept {
  return (buttons & embeddedMouseButtonMask(button)) != 0;
}

struct EmbeddedMouseDecision {
  bool forward = false;
  bool startTrackingLeave = false;
  bool sendLeave = false;
  bool capture = false;
  bool releaseCapture = false;
  EmbeddedMouseButtons cancelButtons = 0;

  bool handled() const noexcept {
    return forward || startTrackingLeave || sendLeave || capture ||
           releaseCapture || cancelButtons != 0;
  }
};

// Pure state machine for the Win32 mouse session associated with one embedded
// surface. It deliberately owns no HWND or WebView2 object so transition and
// cancellation behavior can be verified without a WebView2 runtime.
class EmbeddedMouseSession {
 public:
  EmbeddedMouseDecision move(bool inside) noexcept {
    EmbeddedMouseDecision decision;
    updateHover(inside, decision);
    decision.forward = inside || buttons_ != 0;
    return decision;
  }

  EmbeddedMouseDecision buttonDown(EmbeddedMouseButton button,
                                   bool inside) noexcept {
    EmbeddedMouseDecision decision;
    updateHover(inside, decision);
    if (!inside && buttons_ == 0) return decision;

    decision.forward = true;
    const EmbeddedMouseButtons previous = buttons_;
    buttons_ = static_cast<EmbeddedMouseButtons>(
        buttons_ | embeddedMouseButtonMask(button));
    decision.capture = previous == 0 && buttons_ != 0;
    return decision;
  }

  EmbeddedMouseDecision buttonUp(EmbeddedMouseButton button,
                                 bool inside) noexcept {
    EmbeddedMouseDecision decision;
    const EmbeddedMouseButtons mask = embeddedMouseButtonMask(button);
    if ((buttons_ & mask) == 0) {
      updateHover(inside, decision);
      return decision;
    }

    updateHover(inside, decision);
    decision.forward = true;
    buttons_ = static_cast<EmbeddedMouseButtons>(buttons_ & ~mask);
    decision.releaseCapture = buttons_ == 0;
    return decision;
  }

  EmbeddedMouseDecision nativeLeave() noexcept {
    EmbeddedMouseDecision decision;
    if (hovered_) {
      hovered_ = false;
      decision.sendLeave = true;
    }
    return decision;
  }

  EmbeddedMouseDecision captureLost() noexcept {
    // ReleaseCapture after the final physical UP can synchronously emit
    // WM_CAPTURECHANGED. With no pressed buttons that is not a cancelled
    // embedded session and must not tear down an otherwise valid hover.
    if (buttons_ == 0) return {};
    return cancel(false);
  }

  EmbeddedMouseDecision disable() noexcept { return cancel(true); }

  EmbeddedMouseButtons buttons() const noexcept { return buttons_; }
  bool hovered() const noexcept { return hovered_; }

 private:
  void updateHover(bool inside, EmbeddedMouseDecision& decision) noexcept {
    if (inside && !hovered_) {
      hovered_ = true;
      decision.startTrackingLeave = true;
    } else if (!inside && hovered_) {
      hovered_ = false;
      decision.sendLeave = true;
    }
  }

  EmbeddedMouseDecision cancel(bool releaseNativeCapture) noexcept {
    EmbeddedMouseDecision decision;
    decision.cancelButtons = buttons_;
    // A leave is also required when capture was lost after a native leave:
    // cancellation sends it immediately before the synthetic outside UPs so
    // WebView cannot turn those UPs into clicks.
    decision.sendLeave = hovered_ || buttons_ != 0;
    decision.releaseCapture = releaseNativeCapture && buttons_ != 0;
    buttons_ = 0;
    hovered_ = false;
    return decision;
  }

  EmbeddedMouseButtons buttons_ = 0;
  bool hovered_ = false;
};

class EmbeddedMouseCancellationSink {
 public:
  virtual ~EmbeddedMouseCancellationSink() = default;
  virtual HRESULT sendLeave() noexcept = 0;
  virtual HRESULT sendButtonUp(EmbeddedMouseButton button,
                               EmbeddedMouseButtons remainingButtons)
      noexcept = 0;
};

// Cancellation never short-circuits. LEAVE is deliberately first and every
// active button receives an UP at a surface-local outside point supplied by
// the sink. This clears WebView/DOM pressed state without producing a click.
inline HRESULT runEmbeddedMouseCancellation(
    EmbeddedMouseButtons buttons,
    EmbeddedMouseCancellationSink& sink) noexcept {
  HRESULT firstResult = sink.sendLeave();
  constexpr std::array<EmbeddedMouseButton, 5> kOrder{
      EmbeddedMouseButton::Left, EmbeddedMouseButton::Right,
      EmbeddedMouseButton::Middle, EmbeddedMouseButton::X1,
      EmbeddedMouseButton::X2};
  for (const EmbeddedMouseButton button : kOrder) {
    const EmbeddedMouseButtons mask = embeddedMouseButtonMask(button);
    if ((buttons & mask) == 0) continue;
    buttons = static_cast<EmbeddedMouseButtons>(buttons & ~mask);
    const HRESULT result = sink.sendButtonUp(button, buttons);
    if (SUCCEEDED(firstResult) && FAILED(result)) firstResult = result;
  }
  return firstResult;
}

}  // namespace canvas::windows
