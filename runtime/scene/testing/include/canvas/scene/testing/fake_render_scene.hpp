#pragma once

#include "canvas/scene/render_scene.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace canvas::testing {

class FakeRenderScene final : public IRenderScene {
  public:
    foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
    prepareReplace(std::span<const SceneRecord> records, SceneRevision revision) const override;

    foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
    prepareApply(std::span<const SceneMutation> mutations,
                 SceneRevision beforeRevision,
                 SceneRevision afterRevision) const override;

    void commit(std::unique_ptr<IPreparedRenderSceneUpdate> update) noexcept override;

    foundation::Result<PreciseHit> preciseHitTest(const PreciseHitRequest& request) const override;
    foundation::Result<SceneDrawList>
    buildDrawList(std::span<const ObjectId> backToFront) const override;
    [[nodiscard]] RenderSceneDiagnostics diagnostics() const override;

    void setRejectPrepare(bool reject) {
        _rejectPrepare = reject;
    }
    [[nodiscard]] std::uint64_t stateDigest() const;
    [[nodiscard]] std::span<const SceneRecord> records() const {
        return _records;
    }

  private:
    bool _rejectPrepare = false;
    mutable std::uint64_t _prepareCount = 0;
    std::uint64_t _commitCount = 0;
    SceneRevision _revision;
    std::vector<SceneRecord> _records;
};

} // namespace canvas::testing
