#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace canvas::foundation {

struct ObjectId final {
    std::array<std::uint8_t, 16> bytes{};

    static ObjectId fromUint64(std::uint64_t value) {
        ObjectId result;
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            result.bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
        return result;
    }

    [[nodiscard]] bool isZero() const {
        return *this == ObjectId{};
    }
    auto operator<=>(const ObjectId&) const = default;
};

struct ObjectIdHash final {
    std::size_t operator()(const ObjectId& id) const noexcept {
        std::uint64_t result = 1469598103934665603ULL;
        for (const std::uint8_t byte : id.bytes) {
            result ^= byte;
            result *= 1099511628211ULL;
        }
        return static_cast<std::size_t>(result);
    }
};

} // namespace canvas::foundation
