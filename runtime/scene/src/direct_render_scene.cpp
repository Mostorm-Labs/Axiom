#include "canvas/scene/direct_render_scene.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace canvas {
class DirectRenderScene::PreparedUpdate final : public IPreparedRenderSceneUpdate {
  public:
    SceneRevision revision;
    std::vector<RenderRecord> records;
    std::unordered_map<ObjectId, std::size_t, foundation::ObjectIdHash> index;
};

namespace {

foundation::Error makeError(foundation::ErrorCode code, const char* message) {
    return foundation::Error{code, message};
}

float distanceToRect(const WorldPoint& point, const WorldRect& rect) {
    const float dx = std::max({rect.left - point.x, 0.0F, point.x - rect.right});
    const float dy = std::max({rect.top - point.y, 0.0F, point.y - rect.bottom});
    return std::hypot(dx, dy);
}

} // namespace

foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
DirectRenderScene::prepareRecords(std::vector<RenderRecord> records, SceneRevision revision) const {
    try {
        std::sort(records.begin(),
                  records.end(),
                  [](const RenderRecord& left, const RenderRecord& right) {
                      return left.orderKey < right.orderKey ||
                             (left.orderKey == right.orderKey && left.objectId < right.objectId);
                  });
        auto update = std::make_unique<PreparedUpdate>();
        update->revision = revision;
        update->records = std::move(records);
        update->index.reserve(update->records.size());
        for (std::size_t index = 0; index < update->records.size(); ++index) {
            if (!update->index.emplace(update->records[index].objectId, index).second) {
                return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
                    makeError(foundation::ErrorCode::kDuplicateObject,
                              "DirectRenderScene contains a duplicate ObjectId"));
            }
        }
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::success(
            std::move(update));
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(makeError(
            foundation::ErrorCode::kOutOfMemory, "DirectRenderScene could not prepare update"));
    }
}

foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
DirectRenderScene::prepareReplace(std::span<const SceneRecord> records,
                                  SceneRevision revision) const {
    ++_prepareCount;
    try {
        std::vector<RenderRecord> nextRecords;
        nextRecords.reserve(records.size());
        for (const SceneRecord& record : records) {
            nextRecords.push_back(RenderRecord{
                record.objectId,
                record.orderKey,
                record.worldBounds,
                record.renderPayload,
            });
        }
        return prepareRecords(std::move(nextRecords), revision);
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory,
                      "DirectRenderScene could not prepare replacement"));
    }
}

foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
DirectRenderScene::prepareApply(std::span<const SceneMutation> mutations,
                                SceneRevision beforeRevision,
                                SceneRevision afterRevision) const {
    ++_prepareCount;
    if (beforeRevision != _revision || afterRevision <= beforeRevision) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            makeError(foundation::ErrorCode::kInvalidRevision,
                      "DirectRenderScene delta revision is invalid"));
    }
    try {
        std::vector<RenderRecord> nextRecords = _records;
        for (const SceneMutation& mutation : mutations) {
            const auto found = std::find_if(
                nextRecords.begin(), nextRecords.end(), [&mutation](const RenderRecord& record) {
                    return record.objectId == mutation.objectId;
                });
            switch (mutation.kind) {
            case SceneMutationKind::kInsert:
                if (found != nextRecords.end() || !mutation.after) {
                    return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
                        makeError(foundation::ErrorCode::kInvalidRecord,
                                  "DirectRenderScene insert is inconsistent"));
                }
                nextRecords.push_back(RenderRecord{
                    mutation.after->objectId,
                    mutation.after->orderKey,
                    mutation.after->worldBounds,
                    mutation.after->renderPayload,
                });
                break;
            case SceneMutationKind::kUpdate:
                if (found == nextRecords.end() || !mutation.before || !mutation.after) {
                    return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
                        makeError(foundation::ErrorCode::kInvalidRecord,
                                  "DirectRenderScene update is inconsistent"));
                }
                *found = RenderRecord{
                    mutation.after->objectId,
                    mutation.after->orderKey,
                    mutation.after->worldBounds,
                    mutation.after->renderPayload,
                };
                break;
            case SceneMutationKind::kRemove:
                if (found == nextRecords.end() || !mutation.before || mutation.after) {
                    return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
                        makeError(foundation::ErrorCode::kInvalidRecord,
                                  "DirectRenderScene remove is inconsistent"));
                }
                nextRecords.erase(found);
                break;
            default:
                return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
                    makeError(foundation::ErrorCode::kInvalidRecord,
                              "DirectRenderScene mutation kind is unknown"));
            }
        }
        return prepareRecords(std::move(nextRecords), afterRevision);
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(makeError(
            foundation::ErrorCode::kOutOfMemory, "DirectRenderScene could not prepare delta"));
    }
}

void DirectRenderScene::commit(std::unique_ptr<IPreparedRenderSceneUpdate> prepared) noexcept {
    auto* update = static_cast<PreparedUpdate*>(prepared.get());
    _records.swap(update->records);
    _index.swap(update->index);
    _revision = update->revision;
    ++_commitCount;
}

foundation::Result<PreciseHit>
DirectRenderScene::preciseHitTest(const PreciseHitRequest& request) const {
    if (!std::isfinite(request.worldPoint.x) || !std::isfinite(request.worldPoint.y) ||
        !std::isfinite(request.tolerance) || request.tolerance < 0.0F) {
        return foundation::Result<PreciseHit>::failure(
            makeError(foundation::ErrorCode::kInvalidArgument, "Precise hit request is invalid"));
    }
    const auto found = _index.find(request.objectId);
    if (found == _index.end()) {
        return foundation::Result<PreciseHit>::failure(
            makeError(foundation::ErrorCode::kMissingObject, "Precise hit ObjectId is missing"));
    }
    const float distance = distanceToRect(request.worldPoint, _records[found->second].worldBounds);
    return foundation::Result<PreciseHit>::success(
        PreciseHit{distance <= request.tolerance, distance});
}

foundation::Result<SceneDrawList>
DirectRenderScene::buildDrawList(std::span<const ObjectId> backToFront) const {
    try {
        SceneDrawList drawList{.revision = _revision};
        drawList.items.reserve(backToFront.size());
        for (ObjectId objectId : backToFront) {
            const auto found = _index.find(objectId);
            if (found == _index.end()) {
                return foundation::Result<SceneDrawList>::failure(makeError(
                    foundation::ErrorCode::kMissingObject, "Draw-list ObjectId is missing"));
            }
            const RenderRecord& record = _records[found->second];
            drawList.items.push_back(SceneDrawItem{
                record.objectId,
                record.orderKey,
                record.renderPayload,
            });
        }
        return foundation::Result<SceneDrawList>::success(std::move(drawList));
    } catch (const std::bad_alloc&) {
        return foundation::Result<SceneDrawList>::failure(makeError(
            foundation::ErrorCode::kOutOfMemory, "DirectRenderScene could not build draw list"));
    }
}

RenderSceneDiagnostics DirectRenderScene::diagnostics() const {
    return RenderSceneDiagnostics{
        .revision = _revision,
        .nodeCount = _records.size(),
        .prepareCount = _prepareCount,
        .commitCount = _commitCount,
    };
}

} // namespace canvas
