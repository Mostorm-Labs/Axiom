#pragma once

#include "canvas/core/geometry.h"
#include "canvas/document/document.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace canvas::document {

struct StrokeSweepQuery {
  core::Vec2 from;
  core::Vec2 to;
  float eraserRadius = 0.0F;
  std::size_t workBudget = 0;
};

struct StrokeHitToken {
  std::size_t nodeIndex = 0;
  std::uint64_t cacheIdentity = 0;
  LayerClass layer = LayerClass::Base;

  friend constexpr bool operator==(const StrokeHitToken& left,
                                   const StrokeHitToken& right) noexcept {
    return left.nodeIndex == right.nodeIndex &&
           left.cacheIdentity == right.cacheIdentity &&
           left.layer == right.layer;
  }
};

enum class StrokeHitTestStatus {
  NoHit,
  Hit,
  InvalidQuery,
  BudgetExhausted,
};

struct StrokeHitTestResult {
  StrokeHitTestStatus status = StrokeHitTestStatus::NoHit;
  std::optional<StrokeHitToken> token;
  std::size_t workConsumed = 0;
};

// Allocation-free, read-only query over world/top-left logical coordinates.
// The returned index is usable only while cacheIdentity still matches the
// node at that index. Every visited document node, parent candidate, and
// inspected stroke primitive consumes one unit of the explicit work budget.
StrokeHitTestResult findTopmostStrokeHit(
    const Document& document, const StrokeSweepQuery& query) noexcept;

}  // namespace canvas::document
