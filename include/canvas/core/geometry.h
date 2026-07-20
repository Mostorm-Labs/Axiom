#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace canvas::core {

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;

  friend constexpr bool operator==(const Vec2& lhs, const Vec2& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
  }

  friend constexpr bool operator!=(const Vec2& lhs, const Vec2& rhs) noexcept {
    return !(lhs == rhs);
  }
};

struct Rect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;

  static constexpr Rect fromPoints(Vec2 first, Vec2 second) noexcept {
    const float left = std::min(first.x, second.x);
    const float top = std::min(first.y, second.y);
    return Rect{left, top, std::max(first.x, second.x) - left,
                std::max(first.y, second.y) - top};
  }

  constexpr Rect united(const Rect& other) const noexcept {
    const float left = std::min(x, other.x);
    const float top = std::min(y, other.y);
    const float right = std::max(x + width, other.x + other.width);
    const float bottom = std::max(y + height, other.y + other.height);
    return Rect{left, top, right - left, bottom - top};
  }

  constexpr Rect inflated(float amount) const noexcept {
    return Rect{x - amount, y - amount, width + amount * 2.0F,
                height + amount * 2.0F};
  }

  constexpr bool contains(Vec2 point) const noexcept {
    return point.x >= x && point.x <= x + width && point.y >= y &&
           point.y <= y + height;
  }

  friend constexpr bool operator==(const Rect& lhs, const Rect& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
           lhs.height == rhs.height;
  }

  friend constexpr bool operator!=(const Rect& lhs, const Rect& rhs) noexcept {
    return !(lhs == rhs);
  }
};

struct Transform2D {
  float scaleX = 1.0F;
  float scaleY = 1.0F;
  float translateX = 0.0F;
  float translateY = 0.0F;

  constexpr Vec2 map(Vec2 point) const noexcept {
    return Vec2{point.x * scaleX + translateX,
                point.y * scaleY + translateY};
  }

  Transform2D inverse() const {
    constexpr float kMinimumScale = 1.0e-6F;
    if (std::fabs(scaleX) < kMinimumScale || std::fabs(scaleY) < kMinimumScale) {
      throw std::domain_error("Transform2D is not invertible");
    }
    return Transform2D{1.0F / scaleX, 1.0F / scaleY, -translateX / scaleX,
                       -translateY / scaleY};
  }
};

}  // namespace canvas::core
