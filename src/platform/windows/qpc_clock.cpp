#include "platform/windows/qpc_clock.h"

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
    if (multiplier == 0) break;
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

}  // namespace

std::uint64_t qpcTicksToMicros(std::uint64_t ticks,
                               std::uint64_t frequency) noexcept {
  if (frequency == 0) return 0;
  constexpr std::uint64_t kMicrosPerSecond = 1000000ULL;
  const std::uint64_t seconds = ticks / frequency;
  if (seconds > std::numeric_limits<std::uint64_t>::max() /
                    kMicrosPerSecond) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const std::uint64_t whole = seconds * kMicrosPerSecond;
  const std::uint64_t fractional = multiplyDivide(
      ticks % frequency, kMicrosPerSecond, frequency);
  return fractional > std::numeric_limits<std::uint64_t>::max() - whole
             ? std::numeric_limits<std::uint64_t>::max()
             : whole + fractional;
}

}  // namespace canvas::windows
