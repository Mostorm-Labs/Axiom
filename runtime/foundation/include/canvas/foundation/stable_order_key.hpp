#pragma once

#include <compare>
#include <cstdint>

namespace canvas::foundation {

class StableOrderKey final {
  public:
    constexpr StableOrderKey() = default;
    explicit constexpr StableOrderKey(std::uint64_t value) : _value(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const {
        return _value;
    }
    constexpr auto operator<=>(const StableOrderKey&) const = default;

  private:
    std::uint64_t _value = 0;
};

} // namespace canvas::foundation
