#include "platform/windows/win_pointer_adapter.h"

#include <algorithm>
#include <limits>

namespace canvas::windows {

namespace {

std::uint64_t multiplyDivide(std::uint64_t value, std::uint64_t multiplier,
                             std::uint64_t divisor) {
  std::uint64_t quotient = 0;
  std::uint64_t remainder = 0;
  std::uint64_t termQuotient = value / divisor;
  std::uint64_t termRemainder = value % divisor;

  while (multiplier != 0) {
    if ((multiplier & 1U) != 0) {
      quotient += termQuotient;
      if (termRemainder >= divisor - remainder) {
        ++quotient;
        remainder = termRemainder - (divisor - remainder);
      } else {
        remainder += termRemainder;
      }
    }

    multiplier >>= 1U;
    if (multiplier == 0) {
      break;
    }
    termQuotient *= 2;
    if (termRemainder >= divisor - termRemainder) {
      ++termQuotient;
      termRemainder -= divisor - termRemainder;
    } else {
      termRemainder *= 2;
    }
  }
  return quotient;
}

std::uint64_t qpcToMicros(std::uint64_t ticks, std::uint64_t frequency) {
  if (frequency == 0) {
    return 0;
  }

  // Split the division to avoid floating-point drift and keep the product
  // bounded for the frequencies used by QueryPerformanceCounter.
  const std::uint64_t seconds = ticks / frequency;
  const std::uint64_t remainder = ticks % frequency;
  constexpr std::uint64_t kMicrosPerSecond = 1'000'000;
  if (seconds > std::numeric_limits<std::uint64_t>::max() /
                     kMicrosPerSecond) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const std::uint64_t wholeMicros = seconds * kMicrosPerSecond;
  const std::uint64_t fractionalMicros =
      multiplyDivide(remainder, kMicrosPerSecond, frequency);
  if (wholeMicros > std::numeric_limits<std::uint64_t>::max() -
                        fractionalMicros) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return wholeMicros + fractionalMicros;
}

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
  sample.timestampMicros = qpcToMicros(raw.performanceCount, qpcFrequency);
  sample.screenPosition =
      core::Vec2{static_cast<float>(raw.screenX - clientOriginScreen.x),
                 static_cast<float>(raw.screenY - clientOriginScreen.y)};
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
