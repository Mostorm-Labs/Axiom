#include "platform/macos/macos_mouse_session.h"

#include <limits>

namespace canvas::macos {

std::optional<std::uint64_t> MacosMouseSession::allocatePointerId() noexcept {
  if (nextPointerId_ == 0) {
    return std::nullopt;
  }

  const std::uint64_t allocated = nextPointerId_;
  if (nextPointerId_ == std::numeric_limits<std::uint64_t>::max()) {
    nextPointerId_ = 0;
  } else {
    ++nextPointerId_;
  }
  return allocated;
}

MacosMouseSessionOutput MacosMouseSession::consume(
    const RawMacMouseEvent& raw) {
  MacosMouseSessionOutput output;
  const auto append = [&output](const input::PointerSample& sample) {
    output.samples[output.count++] = sample;
  };

  if (raw.phase == MacMousePhase::Cancel) {
    if (!activePointerId_) {
      return output;
    }
    append(MacosPointerAdapter::normalize(raw, *activePointerId_));
    activePointerId_.reset();
    lastSample_.reset();
    return output;
  }

  if (raw.buttonNumber != 0) {
    return output;
  }

  if (raw.phase == MacMousePhase::Down) {
    if (const auto cancelled = cancel()) {
      append(*cancelled);
    }

    const std::optional<std::uint64_t> pointerId = allocatePointerId();
    if (!pointerId) {
      return output;
    }

    activePointerId_ = *pointerId;
    input::PointerSample down = MacosPointerAdapter::normalize(raw, *pointerId);
    lastSample_ = down;
    append(down);
    return output;
  }

  if (!activePointerId_) {
    return output;
  }

  input::PointerSample sample =
      MacosPointerAdapter::normalize(raw, *activePointerId_);
  append(sample);
  if (raw.phase == MacMousePhase::Up) {
    activePointerId_.reset();
    lastSample_.reset();
  } else {
    lastSample_ = sample;
  }
  return output;
}

std::optional<input::PointerSample> MacosMouseSession::cancel() {
  if (!activePointerId_ || !lastSample_) {
    return std::nullopt;
  }

  input::PointerSample cancelled = *lastSample_;
  cancelled.phase = input::PointerPhase::Cancel;
  activePointerId_.reset();
  lastSample_.reset();
  return cancelled;
}

}  // namespace canvas::macos
