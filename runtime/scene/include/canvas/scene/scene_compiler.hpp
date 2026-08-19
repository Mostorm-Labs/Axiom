#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/scene/scene_types.hpp"

namespace canvas {

class ICompiledSceneSource {
  public:
    virtual ~ICompiledSceneSource() = default;

    virtual foundation::Result<CompiledSceneSnapshot> compileFull() const = 0;
    virtual foundation::Result<CompiledSceneDelta> compileDelta() const = 0;
};

} // namespace canvas
