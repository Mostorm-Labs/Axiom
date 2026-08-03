#pragma once

#include "platform/macos/macos_pointer_adapter.h"

#include <array>
#include <cstddef>
#include <optional>

namespace canvas::macos {

struct MacosMouseSessionOutput {
  std::array<input::PointerSample, 2> samples;
  std::size_t count = 0;

  bool empty() const noexcept { return count == 0; }
  std::size_t size() const noexcept { return count; }
  const input::PointerSample& operator[](std::size_t index) const noexcept {
    return samples[index];
  }
};

class MacosMouseSession {
 public:
  MacosMouseSessionOutput consume(const RawMacMouseEvent& raw);
  std::optional<input::PointerSample> cancel();

  bool active() const noexcept { return activePointerId_.has_value(); }

 private:
  std::optional<std::uint64_t> allocatePointerId() noexcept;

  std::uint64_t nextPointerId_ = 1;
  std::optional<std::uint64_t> activePointerId_;
  std::optional<input::PointerSample> lastSample_;
};

}  // namespace canvas::macos
