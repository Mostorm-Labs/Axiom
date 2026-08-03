#pragma once

#include "canvas/input/pointer_sample.h"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace canvas::windows {

struct RawPenPoint {
  std::uint64_t pointerId = 0;
  std::uint64_t performanceCount = 0;
  long screenX = 0;
  long screenY = 0;
  std::uint32_t pressure = 512;
  int tiltX = 0;
  int tiltY = 0;
};

class WinPointerAdapter {
 public:
  static input::PointerSample normalize(
      const RawPenPoint& raw, POINT clientOriginScreen,
      std::uint64_t qpcFrequency, input::PointerPhase phase,
      input::PointerKind kind = input::PointerKind::Pen);

  // Windows history APIs return newest-first records. The normalized result is
  // intentionally oldest-first so consumers can process it chronologically.
  static std::vector<input::PointerSample> normalizeHistory(
      const std::vector<RawPenPoint>& newestFirst, POINT origin,
      std::uint64_t qpcFrequency, input::PointerPhase phase,
      input::PointerKind kind = input::PointerKind::Pen);

  static std::vector<input::PointerSample> readPenHistory(
      HWND window, UINT32 pointerId, input::PointerPhase phase);
  static std::vector<input::PointerSample> readTouchHistory(
      HWND window, UINT32 pointerId, input::PointerPhase phase);
};

}  // namespace canvas::windows
