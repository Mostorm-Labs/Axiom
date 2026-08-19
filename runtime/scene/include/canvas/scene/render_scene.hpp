#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/scene/scene_types.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace canvas {

class IPreparedRenderSceneUpdate {
  public:
    virtual ~IPreparedRenderSceneUpdate() = default;
};

struct PreciseHitRequest final {
    ObjectId objectId;
    WorldPoint worldPoint;
    float tolerance = 0.0F;
};

struct PreciseHit final {
    bool hit = false;
    float distance = 0.0F;
};

struct SceneDrawItem final {
    ObjectId objectId;
    SceneOrderKey orderKey;
    RenderPayloadRef renderPayload;
};

struct SceneDrawList final {
    SceneRevision revision;
    std::vector<SceneDrawItem> items;
};

struct RenderSceneDiagnostics final {
    SceneRevision revision;
    std::uint64_t nodeCount = 0;
    std::uint64_t prepareCount = 0;
    std::uint64_t commitCount = 0;
};

class IRenderScene {
  public:
    virtual ~IRenderScene() = default;

    virtual foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
    prepareReplace(std::span<const SceneRecord> records, SceneRevision revision) const = 0;

    virtual foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
    prepareApply(std::span<const SceneMutation> mutations,
                 SceneRevision beforeRevision,
                 SceneRevision afterRevision) const = 0;

    virtual void commit(std::unique_ptr<IPreparedRenderSceneUpdate> update) noexcept = 0;

    virtual foundation::Result<PreciseHit>
    preciseHitTest(const PreciseHitRequest& request) const = 0;

    virtual foundation::Result<SceneDrawList>
    buildDrawList(std::span<const ObjectId> backToFront) const = 0;

    [[nodiscard]] virtual RenderSceneDiagnostics diagnostics() const = 0;
};

} // namespace canvas
