#pragma once

#include "canvas/core/geometry.h"
#include "canvas/document/node.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace canvas::document {

namespace detail {
inline void validateParentBounds(const core::Rect& parent) {
  if (!std::isfinite(parent.x) || !std::isfinite(parent.y) ||
      !std::isfinite(parent.width) || !std::isfinite(parent.height) ||
      parent.width <= 0.0F || parent.height <= 0.0F) {
    throw std::domain_error("Embedded parent has invalid bounds");
  }
}

inline float checkedFinite(float value, const char* message) {
  if (!std::isfinite(value)) {
    throw std::domain_error(message);
  }
  return value;
}

inline core::Vec2 checkedFinite(core::Vec2 value, const char* message) {
  if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
    throw std::domain_error(message);
  }
  return value;
}
}  // namespace detail

inline core::Vec2 toParentNormalized(core::Vec2 world,
                                     const core::Rect& parent) {
  detail::validateParentBounds(parent);
  if (!std::isfinite(world.x) || !std::isfinite(world.y)) {
    throw std::domain_error("Embedded point has invalid coordinates");
  }
  return detail::checkedFinite(
      core::Vec2{(world.x - parent.x) / parent.width,
                 (world.y - parent.y) / parent.height},
      "Embedded normalized point is not finite");
}

inline core::Vec2 fromParentNormalized(core::Vec2 local,
                                       const core::Rect& parent) {
  detail::validateParentBounds(parent);
  if (!std::isfinite(local.x) || !std::isfinite(local.y)) {
    throw std::domain_error("Embedded point has invalid coordinates");
  }
  return detail::checkedFinite(
      core::Vec2{parent.x + local.x * parent.width,
                 parent.y + local.y * parent.height},
      "Embedded world point is not finite");
}

inline float toParentRelativeWidth(float worldWidth,
                                   const core::Rect& parent) {
  detail::validateParentBounds(parent);
  if (!std::isfinite(worldWidth)) {
    throw std::domain_error("Embedded stroke has invalid width");
  }
  return detail::checkedFinite(
      worldWidth / std::min(parent.width, parent.height),
      "Embedded relative width is not finite");
}

inline float fromParentRelativeWidth(float relativeWidth,
                                     const core::Rect& parent) {
  detail::validateParentBounds(parent);
  if (!std::isfinite(relativeWidth)) {
    throw std::domain_error("Embedded stroke has invalid width");
  }
  return detail::checkedFinite(
      relativeWidth * std::min(parent.width, parent.height),
      "Embedded world width is not finite");
}

inline StrokeNode attachStrokeToParent(StrokeNode stroke,
                                       const core::Rect& parent) {
  for (auto& point : stroke.points) {
    point.position = toParentNormalized(point.position, parent);
  }
  stroke.width = toParentRelativeWidth(stroke.width, parent);
  stroke.coordinateSpace = StrokeCoordinateSpace::ParentNormalized;
  return stroke;
}

inline StrokeNode resolveAttachedStroke(StrokeNode stroke,
                                        const core::Rect& parent) {
  for (auto& point : stroke.points) {
    point.position = fromParentNormalized(point.position, parent);
  }
  stroke.width = fromParentRelativeWidth(stroke.width, parent);
  stroke.coordinateSpace = StrokeCoordinateSpace::World;
  return stroke;
}

}  // namespace canvas::document
