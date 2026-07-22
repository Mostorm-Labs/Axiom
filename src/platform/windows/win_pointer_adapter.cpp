#include "platform/windows/win_pointer_adapter.h"
#include "platform/windows/qpc_clock.h"

#include <algorithm>

namespace canvas::windows {

namespace {

std::vector<input::PointerSample> readHistoryOrigin(
    HWND window, std::uint64_t qpcFrequency,
    const std::vector<RawPenPoint>& points, input::PointerPhase phase,
    input::PointerKind kind) {
  POINT origin{0, 0};
  if (window != nullptr) {
    ClientToScreen(window, &origin);
  }
  return WinPointerAdapter::normalizeHistory(points, origin, qpcFrequency,
                                             phase, kind);
}

}  // namespace

input::PointerSample WinPointerAdapter::normalize(
    const RawPenPoint& raw, POINT clientOriginScreen,
    std::uint64_t qpcFrequency, input::PointerPhase phase,
    input::PointerKind kind) {
  const std::uint32_t clampedPressure =
      std::min<std::uint32_t>(1024, raw.pressure);
  input::PointerSample sample;
  sample.pointerId = raw.pointerId;
  sample.timestampMicros =
      qpcTicksToMicros(raw.performanceCount, qpcFrequency);
  sample.screenPosition =
      core::Vec2{static_cast<float>(raw.screenX) -
                     static_cast<float>(clientOriginScreen.x),
                 static_cast<float>(raw.screenY) -
                     static_cast<float>(clientOriginScreen.y)};
  sample.pressure = static_cast<float>(clampedPressure) / 1024.0F;
  sample.tiltXDegrees = static_cast<float>(raw.tiltX);
  sample.tiltYDegrees = static_cast<float>(raw.tiltY);
  sample.kind = kind;
  sample.phase = phase;
  sample.predicted = false;
  return sample;
}

std::vector<input::PointerSample> WinPointerAdapter::normalizeHistory(
    const std::vector<RawPenPoint>& newestFirst, POINT origin,
    std::uint64_t qpcFrequency, input::PointerPhase phase,
    input::PointerKind kind) {
  std::vector<input::PointerSample> result;
  result.reserve(newestFirst.size());
  for (auto it = newestFirst.rbegin(); it != newestFirst.rend(); ++it) {
    result.push_back(normalize(*it, origin, qpcFrequency, phase, kind));
  }
  return result;
}

std::vector<input::PointerSample> WinPointerAdapter::readPenHistory(
    HWND window, UINT32 pointerId, input::PointerPhase phase) {
  UINT32 count = 0;
  GetPointerPenInfoHistory(pointerId, &count, nullptr);
  if (count == 0) {
    return {};
  }
  std::vector<POINTER_PEN_INFO> history(count);
  if (!GetPointerPenInfoHistory(pointerId, &count, history.data())) {
    return {};
  }

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  std::vector<RawPenPoint> points;
  points.reserve(count);
  for (UINT32 index = 0; index < count; ++index) {
    const auto& pen = history[index];
    const auto& info = pen.pointerInfo;
    points.push_back(RawPenPoint{
        static_cast<std::uint64_t>(info.pointerId),
        static_cast<std::uint64_t>(info.PerformanceCount),
        info.ptPixelLocation.x,
        info.ptPixelLocation.y,
        pen.pressure,
        static_cast<int>(pen.tiltX),
        static_cast<int>(pen.tiltY),
    });
  }
  return readHistoryOrigin(window, static_cast<std::uint64_t>(frequency.QuadPart),
                           points, phase, input::PointerKind::Pen);
}

std::vector<input::PointerSample> WinPointerAdapter::readTouchHistory(
    HWND window, UINT32 pointerId, input::PointerPhase phase) {
  UINT32 count = 0;
  GetPointerTouchInfoHistory(pointerId, &count, nullptr);
  if (count == 0) {
    return {};
  }
  std::vector<POINTER_TOUCH_INFO> history(count);
  if (!GetPointerTouchInfoHistory(pointerId, &count, history.data())) {
    return {};
  }

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  std::vector<RawPenPoint> points;
  points.reserve(count);
  for (UINT32 index = 0; index < count; ++index) {
    const auto& touch = history[index];
    const auto& info = touch.pointerInfo;
    points.push_back(RawPenPoint{
        static_cast<std::uint64_t>(info.pointerId),
        static_cast<std::uint64_t>(info.PerformanceCount),
        info.ptPixelLocation.x,
        info.ptPixelLocation.y,
        512,
        0,
        0,
    });
  }
  return readHistoryOrigin(window, static_cast<std::uint64_t>(frequency.QuadPart),
                           points, phase, input::PointerKind::Touch);
}

}  // namespace canvas::windows
