#pragma once

#include "canvas/scene/render_scene.hpp"

#include <cstdint>
#include <memory>

namespace canvas {

struct ShadowRenderDiagnostics final {
    RenderSceneDiagnostics primary;
    RenderSceneDiagnostics shadow;
    std::uint64_t mismatchCount = 0;
};

// Experimental RF-01 shadow participant. It runs two RenderScene
// implementations against the same snapshot/delta and compares observable
// draw and precise-hit results without exposing either implementation type.
class ShadowRenderScene final : public IRenderScene {
  public:
    ShadowRenderScene(std::unique_ptr<IRenderScene> primary,
                      std::unique_ptr<IRenderScene> shadow);

    foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
    prepareReplace(std::span<const SceneRecord> records, SceneRevision revision) const override;

    foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
    prepareApply(std::span<const SceneMutation> mutations,
                 SceneRevision beforeRevision,
                 SceneRevision afterRevision) const override;

    void commit(std::unique_ptr<IPreparedRenderSceneUpdate> update) noexcept override;

    foundation::Result<PreciseHit>
    preciseHitTest(const PreciseHitRequest& request) const override;
    foundation::Result<SceneDrawList>
    buildDrawList(std::span<const ObjectId> backToFront) const override;
    [[nodiscard]] RenderSceneDiagnostics diagnostics() const override;
    [[nodiscard]] ShadowRenderDiagnostics shadowDiagnostics() const;

  private:
    class PreparedUpdate;

    foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>> preparePair(
        foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>> primary,
        foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>> shadow) const;

    std::unique_ptr<IRenderScene> _primary;
    std::unique_ptr<IRenderScene> _shadow;
    mutable std::uint64_t _mismatchCount = 0;
};

} // namespace canvas
