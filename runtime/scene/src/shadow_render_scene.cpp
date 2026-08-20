#include "canvas/scene/shadow_render_scene.hpp"

#include <cassert>
#include <new>
#include <utility>

namespace canvas {
namespace {

foundation::Error makeError(const char* message) {
    return foundation::Error{foundation::ErrorCode::kParticipantRejected, message};
}

bool equal(const SceneDrawList& left, const SceneDrawList& right) {
    if (left.revision != right.revision || left.items.size() != right.items.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.items.size(); ++index) {
        if (left.items[index].objectId != right.items[index].objectId ||
            left.items[index].orderKey != right.items[index].orderKey ||
            left.items[index].renderPayload != right.items[index].renderPayload) {
            return false;
        }
    }
    return true;
}

} // namespace

class ShadowRenderScene::PreparedUpdate final : public IPreparedRenderSceneUpdate {
  public:
    PreparedUpdate(std::unique_ptr<IPreparedRenderSceneUpdate> primary,
                   std::unique_ptr<IPreparedRenderSceneUpdate> shadow)
        : primary(std::move(primary)), shadow(std::move(shadow)) {}

    std::unique_ptr<IPreparedRenderSceneUpdate> primary;
    std::unique_ptr<IPreparedRenderSceneUpdate> shadow;
};

ShadowRenderScene::ShadowRenderScene(std::unique_ptr<IRenderScene> primary,
                                     std::unique_ptr<IRenderScene> shadow)
    : _primary(std::move(primary)), _shadow(std::move(shadow)) {
    assert(_primary != nullptr);
    assert(_shadow != nullptr);
}

foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
ShadowRenderScene::preparePair(
    foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>> primary,
    foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>> shadow) const {
    if (!primary) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            primary.error());
    }
    if (!shadow) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            shadow.error());
    }
    if (!primary.value() || !shadow.value()) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            makeError("Shadow RenderScene participant returned an empty prepared update"));
    }
    try {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::success(
            std::make_unique<PreparedUpdate>(std::move(primary.value()),
                                             std::move(shadow.value())));
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            foundation::Error{foundation::ErrorCode::kOutOfMemory,
                              "Shadow RenderScene could not prepare pair"});
    }
}

foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
ShadowRenderScene::prepareReplace(std::span<const SceneRecord> records,
                                  SceneRevision revision) const {
    return preparePair(_primary->prepareReplace(records, revision),
                       _shadow->prepareReplace(records, revision));
}

foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
ShadowRenderScene::prepareApply(std::span<const SceneMutation> mutations,
                                SceneRevision beforeRevision,
                                SceneRevision afterRevision) const {
    return preparePair(_primary->prepareApply(mutations, beforeRevision, afterRevision),
                       _shadow->prepareApply(mutations, beforeRevision, afterRevision));
}

void ShadowRenderScene::commit(std::unique_ptr<IPreparedRenderSceneUpdate> prepared) noexcept {
    auto* update = static_cast<PreparedUpdate*>(prepared.get());
    _primary->commit(std::move(update->primary));
    _shadow->commit(std::move(update->shadow));
}

foundation::Result<PreciseHit>
ShadowRenderScene::preciseHitTest(const PreciseHitRequest& request) const {
    auto primary = _primary->preciseHitTest(request);
    if (!primary) {
        return primary;
    }
    auto shadow = _shadow->preciseHitTest(request);
    if (!shadow) {
        return shadow;
    }
    if (primary.value().hit != shadow.value().hit ||
        primary.value().distance != shadow.value().distance) {
        ++_mismatchCount;
        return foundation::Result<PreciseHit>::failure(
            makeError("Shadow RenderScene precise hit mismatch"));
    }
    return primary;
}

foundation::Result<SceneDrawList>
ShadowRenderScene::buildDrawList(std::span<const ObjectId> backToFront) const {
    auto primary = _primary->buildDrawList(backToFront);
    if (!primary) {
        return primary;
    }
    auto shadow = _shadow->buildDrawList(backToFront);
    if (!shadow) {
        return shadow;
    }
    if (!equal(primary.value(), shadow.value())) {
        ++_mismatchCount;
        return foundation::Result<SceneDrawList>::failure(
            makeError("Shadow RenderScene draw-list mismatch"));
    }
    return primary;
}

RenderSceneDiagnostics ShadowRenderScene::diagnostics() const {
    return _primary->diagnostics();
}

ShadowRenderDiagnostics ShadowRenderScene::shadowDiagnostics() const {
    return ShadowRenderDiagnostics{
        .primary = _primary->diagnostics(),
        .shadow = _shadow->diagnostics(),
        .mismatchCount = _mismatchCount,
    };
}

} // namespace canvas
