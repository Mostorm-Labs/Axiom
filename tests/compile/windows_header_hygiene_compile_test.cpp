#ifndef NOMINMAX
#error "NOMINMAX must be defined for every Windows translation unit"
#endif

#include <windows.h>

#ifdef min
#error "Windows.h leaked its min macro"
#endif

#ifdef max
#error "Windows.h leaked its max macro"
#endif

#include <algorithm>
#include <limits>

#include "include/core/SkRect.h"

constexpr WORD kSystemArrowCursorId = 32512U;

static_assert(std::min(2, 3) == 2,
              "std::min must remain callable after Windows.h");
static_assert(std::numeric_limits<DWORD>::max() > 0,
              "numeric_limits::max must remain callable after Windows.h");

SkRect canvasWindowsHeaderHygieneCompileProbe() {
  return SkRect::MakeLTRB(0.0F, 0.0F, 1.0F, 1.0F);
}

HCURSOR canvasWindowsWideCursorResourceCompileProbe() {
  return LoadCursorW(nullptr, MAKEINTRESOURCEW(kSystemArrowCursorId));
}
