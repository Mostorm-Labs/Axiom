#pragma once

#include <algorithm>
#include <cmath>

namespace canvas::foundation {

struct WorldPoint final {
    float x = 0.0F;
    float y = 0.0F;
};

struct WorldRect final {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    [[nodiscard]] bool isFiniteAndOrdered() const {
        return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
               std::isfinite(bottom) && left <= right && top <= bottom;
    }

    [[nodiscard]] bool intersects(const WorldRect& other) const {
        return left < other.right && right > other.left && top < other.bottom && bottom > other.top;
    }

    [[nodiscard]] bool operator==(const WorldRect&) const = default;
};

inline WorldRect unionRects(const WorldRect& first, const WorldRect& second) {
    return WorldRect{
        std::min(first.left, second.left),
        std::min(first.top, second.top),
        std::max(first.right, second.right),
        std::max(first.bottom, second.bottom),
    };
}

inline WorldRect canonicalizeRect(WorldRect rect) {
    rect.left = rect.left == 0.0F ? 0.0F : rect.left;
    rect.top = rect.top == 0.0F ? 0.0F : rect.top;
    rect.right = rect.right == 0.0F ? 0.0F : rect.right;
    rect.bottom = rect.bottom == 0.0F ? 0.0F : rect.bottom;
    return rect;
}

} // namespace canvas::foundation
