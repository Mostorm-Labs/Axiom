#pragma once

#include "canvas/scene/render_scene.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace canvas {

class DirectRenderScene final : public IRenderScene {
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

  private:
    class PreparedUpdate;

    struct RenderRecord final {
        ObjectId objectId;
        SceneOrderKey orderKey;
        WorldRect worldBounds;
        RenderPayloadRef renderPayload;
    };

    foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
    prepareRecords(std::vector<RenderRecord> records, SceneRevision revision) const;

    mutable std::uint64_t _prepareCount = 0;
    std::uint64_t _commitCount = 0;
    SceneRevision _revision;
    std::vector<RenderRecord> _records;
    std::unordered_map<ObjectId, std::size_t, foundation::ObjectIdHash> _index;
};

} // namespace canvas
