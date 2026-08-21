#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/scene/scene.hpp"
#include "canvas/scene/scene_compiler.hpp"

#include <cstdint>
#include <optional>

namespace canvas {

enum class SceneSyncDisposition : std::uint8_t {
    kAppliedIncremental,
    kRebuiltFull,
};

struct SceneSyncReceipt final {
    SceneRevision revision;
    SceneSyncDisposition disposition = SceneSyncDisposition::kAppliedIncremental;
    SceneApplyReceipt apply;
    std::optional<foundation::Error> incrementalFailure;
};

class SceneBinding final {
  public:
    explicit SceneBinding(Scene& scene) : _scene(scene) {}

    foundation::Result<SceneSyncReceipt> rebuild(const ICompiledSceneSource& source);
    foundation::Result<SceneSyncReceipt> synchronize(const ICompiledSceneSource& source);

  private:
    foundation::Result<SceneSyncReceipt>
    rebuildAfterIncrementalFailure(const ICompiledSceneSource& source,
                                   foundation::Error incrementalFailure);

    Scene& _scene;
};

} // namespace canvas
