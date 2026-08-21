#pragma once

#include <compare>
#include <cstdint>

namespace canvas::foundation {

template <typename Tag> class Revision final {
  public:
    constexpr Revision() = default;
    explicit constexpr Revision(std::uint64_t value) : _value(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const {
        return _value;
    }
    [[nodiscard]] constexpr bool isZero() const {
        return _value == 0;
    }
    constexpr auto operator<=>(const Revision&) const = default;

  private:
    std::uint64_t _value = 0;
};

struct SceneRevisionTag;
struct ContentRevisionTag;

using SceneRevision = Revision<SceneRevisionTag>;
using ContentRevision = Revision<ContentRevisionTag>;

} // namespace canvas::foundation
