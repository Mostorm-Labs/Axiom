#include "canvas/scene/scene_binding.hpp"

#include <utility>

namespace canvas {

foundation::Result<SceneSyncReceipt> SceneBinding::rebuild(const ICompiledSceneSource& source) {
    auto snapshotResult = source.compileFull();
    if (!snapshotResult) {
        return foundation::Result<SceneSyncReceipt>::failure(snapshotResult.error());
    }
    auto applyResult = _scene.replace(std::move(snapshotResult.value()));
    if (!applyResult) {
        return foundation::Result<SceneSyncReceipt>::failure(applyResult.error());
    }
    SceneApplyReceipt apply = std::move(applyResult.value());
    return foundation::Result<SceneSyncReceipt>::success(SceneSyncReceipt{
        .revision = apply.afterRevision,
        .disposition = SceneSyncDisposition::kRebuiltFull,
        .apply = std::move(apply),
    });
}

foundation::Result<SceneSyncReceipt> SceneBinding::synchronize(const ICompiledSceneSource& source) {
    auto deltaResult = source.compileDelta();
    if (!deltaResult) {
        if (deltaResult.error().code != foundation::ErrorCode::kRequiresFullRebuild) {
            return foundation::Result<SceneSyncReceipt>::failure(deltaResult.error());
        }
        return rebuildAfterIncrementalFailure(source, deltaResult.error());
    }

    auto applyResult = _scene.apply(std::move(deltaResult.value()));
    if (!applyResult) {
        if (applyResult.error().code != foundation::ErrorCode::kRequiresFullRebuild) {
            return foundation::Result<SceneSyncReceipt>::failure(applyResult.error());
        }
        return rebuildAfterIncrementalFailure(source, applyResult.error());
    }

    SceneApplyReceipt apply = std::move(applyResult.value());
    return foundation::Result<SceneSyncReceipt>::success(SceneSyncReceipt{
        .revision = apply.afterRevision,
        .disposition = SceneSyncDisposition::kAppliedIncremental,
        .apply = std::move(apply),
    });
}

foundation::Result<SceneSyncReceipt>
SceneBinding::rebuildAfterIncrementalFailure(const ICompiledSceneSource& source,
                                             foundation::Error incrementalFailure) {
    auto snapshotResult = source.compileFull();
    if (!snapshotResult) {
        return foundation::Result<SceneSyncReceipt>::failure(snapshotResult.error());
    }
    auto applyResult = _scene.replace(std::move(snapshotResult.value()));
    if (!applyResult) {
        return foundation::Result<SceneSyncReceipt>::failure(applyResult.error());
    }
    SceneApplyReceipt apply = std::move(applyResult.value());
    return foundation::Result<SceneSyncReceipt>::success(SceneSyncReceipt{
        .revision = apply.afterRevision,
        .disposition = SceneSyncDisposition::kRebuiltFull,
        .apply = std::move(apply),
        .incrementalFailure = std::move(incrementalFailure),
    });
}

} // namespace canvas
