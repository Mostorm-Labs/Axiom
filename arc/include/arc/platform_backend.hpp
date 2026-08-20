#ifndef ARC_PLATFORM_BACKEND_HPP_
#define ARC_PLATFORM_BACKEND_HPP_

#include <memory>

#include "arc/arc.hpp"

namespace arc {

// Platform factories are declared in arc.hpp. This compatibility header keeps
// host code from importing native OS or GPU types.

}  // namespace arc

#endif  // ARC_PLATFORM_BACKEND_HPP_
