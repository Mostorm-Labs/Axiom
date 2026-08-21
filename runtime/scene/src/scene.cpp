#include "canvas/scene/scene.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <new>
#include <unordered_set>
#include <utility>

namespace canvas {
namespace {

foundation::Error makeError(foundation::ErrorCode code, const char* message) {
    return foundation::Error{code, message};
}

std::vector<SpatialRecord> makeSpatialRecords(std::span<const SceneRecord> records) {
    std::vector<SpatialRecord> spatialRecords;
    spatialRecords.reserve(records.size());
    for (const SceneRecord& record : records) {
        spatialRecords.push_back(SpatialRecord{record.objectId, record.worldBounds});
    }
    return spatialRecords;
}

std::vector<SpatialMutation> makeSpatialMutations(std::span<const SceneMutation> mutations) {
    std::vector<SpatialMutation> spatialMutations;
    spatialMutations.reserve(mutations.size());
    for (const SceneMutation& mutation : mutations) {
        spatialMutations.push_back(SpatialMutation{
            .kind = mutation.kind,
            .objectId = mutation.objectId,
            .before = mutation.before ? std::optional(mutation.before->worldBounds) : std::nullopt,
            .after = mutation.after ? std::optional(mutation.after->worldBounds) : std::nullopt,
        });
    }
    return spatialMutations;
}

DamageSet damageForDelta(const CompiledSceneDelta& delta) {
    DamageSet damage{
        .afterExclusive = delta.beforeRevision,
        .throughInclusive = delta.afterRevision,
        .fullScene = false,
        .rects = {},
    };
    damage.rects.reserve(delta.mutations.size());
    for (const SceneMutation& mutation : delta.mutations) {
        WorldRect dirty =
            mutation.before ? mutation.before->worldBounds : mutation.after->worldBounds;
        if (mutation.before && mutation.after) {
            dirty =
                foundation::unionRects(mutation.before->worldBounds, mutation.after->worldBounds);
        }
        damage.rects.push_back(DamageRect{dirty, DamageReason::kContent});
    }
    return damage;
}

bool isVisible(const SceneRecord& record) {
    return (static_cast<std::uint32_t>(record.flags) &
            static_cast<std::uint32_t>(SceneRecordFlags::kVisible)) != 0;
}

bool hasFlag(SceneRecordFlags value, SceneRecordFlags flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

std::uint32_t kindBit(SceneObjectKind kind) {
    switch (kind) {
    case SceneObjectKind::kShape:
        return static_cast<std::uint32_t>(HitTestKindMask::kShape);
    case SceneObjectKind::kImage:
        return static_cast<std::uint32_t>(HitTestKindMask::kImage);
    case SceneObjectKind::kVectorPath:
        return static_cast<std::uint32_t>(HitTestKindMask::kVectorPath);
    case SceneObjectKind::kRichText:
        return static_cast<std::uint32_t>(HitTestKindMask::kRichText);
    case SceneObjectKind::kVectorStroke:
        return static_cast<std::uint32_t>(HitTestKindMask::kVectorStroke);
    case SceneObjectKind::kDabStroke:
        return static_cast<std::uint32_t>(HitTestKindMask::kDabStroke);
    }
    return 0U;
}

bool hasKnownHitKinds(HitTestKindMask kinds) {
    return (static_cast<std::uint32_t>(kinds) &
            ~static_cast<std::uint32_t>(HitTestKindMask::kAll)) == 0U;
}

} // namespace

const SceneRecord* SceneReadView::find(ObjectId objectId) const {
    const auto found =
        std::find_if(_records.begin(), _records.end(), [objectId](const SceneRecord& record) {
            return record.objectId == objectId;
        });
    return found == _records.end() ? nullptr : &*found;
}

Scene::Scene(std::unique_ptr<IRenderScene> renderScene, std::unique_ptr<ISpatialIndex> spatialIndex)
    : _renderScene(std::move(renderScene)), _spatialIndex(std::move(spatialIndex)) {
    assert(_renderScene != nullptr);
    assert(_spatialIndex != nullptr);
}

foundation::Result<SceneApplyReceipt> Scene::replace(CompiledSceneSnapshot snapshot) {
    if (snapshot.sourceRevision.isZero() || snapshot.sourceRevision <= _revision) {
        return foundation::Result<SceneApplyReceipt>::failure(makeError(
            foundation::ErrorCode::kInvalidRevision, "Snapshot revision must advance Scene"));
    }

    auto recordsResult = _records.prepareReplace(snapshot.records);
    if (!recordsResult) {
        return foundation::Result<SceneApplyReceipt>::failure(recordsResult.error());
    }
    SceneRecordStore::PreparedUpdate preparedRecords = std::move(recordsResult.value());

    try {
        std::vector<SpatialRecord> spatialRecords = makeSpatialRecords(preparedRecords.records());
        auto renderResult =
            _renderScene->prepareReplace(preparedRecords.records(), snapshot.sourceRevision);
        if (!renderResult) {
            return foundation::Result<SceneApplyReceipt>::failure(renderResult.error());
        }
        if (!renderResult.value()) {
            return foundation::Result<SceneApplyReceipt>::failure(
                makeError(foundation::ErrorCode::kParticipantRejected,
                          "Render participant returned an empty prepared update"));
        }

        auto spatialResult = _spatialIndex->prepareReplace(spatialRecords, snapshot.sourceRevision);
        if (!spatialResult) {
            return foundation::Result<SceneApplyReceipt>::failure(spatialResult.error());
        }
        if (!spatialResult.value()) {
            return foundation::Result<SceneApplyReceipt>::failure(
                makeError(foundation::ErrorCode::kParticipantRejected,
                          "Spatial participant returned an empty prepared update"));
        }

        const WorldRect oldContentBounds = _records.contentBounds();
        const WorldRect newContentBounds = preparedRecords.contentBounds();
        auto damageResult = _damageTracker.prepareReplace(
            _revision, snapshot.sourceRevision, oldContentBounds, newContentBounds);
        if (!damageResult) {
            return foundation::Result<SceneApplyReceipt>::failure(damageResult.error());
        }

        SceneApplyReceipt receipt{
            .beforeRevision = _revision,
            .afterRevision = snapshot.sourceRevision,
            .recordsTouched = preparedRecords.records().size(),
            .renderNodesTouched = preparedRecords.records().size(),
            .spatialRecordsTouched = preparedRecords.records().size(),
            .damage =
                DamageSet{
                    .afterExclusive = _revision,
                    .throughInclusive = snapshot.sourceRevision,
                    .fullScene = true,
                    .rects = {DamageRect{
                        .worldRect = foundation::unionRects(oldContentBounds, newContentBounds),
                        .reasons = DamageReason::kFullRebuild,
                    }},
                },
        };

        _commitDiagnostics = SceneCommitDiagnostics{
            .transactionCount = _commitDiagnostics.transactionCount + 1,
        };
        std::uint8_t stage = 0;
        _records.commit(std::move(preparedRecords));
        _commitDiagnostics.recordStoreStage = ++stage;
        _renderScene->commit(std::move(renderResult.value()));
        _commitDiagnostics.renderSceneStage = ++stage;
        _spatialIndex->commit(std::move(spatialResult.value()));
        _commitDiagnostics.spatialIndexStage = ++stage;
        _damageTracker.commit(std::move(damageResult.value()));
        _commitDiagnostics.damageStage = ++stage;
        _revision = snapshot.sourceRevision;
        _commitDiagnostics.revisionStage = ++stage;
        return foundation::Result<SceneApplyReceipt>::success(std::move(receipt));
    } catch (const std::bad_alloc&) {
        return foundation::Result<SceneApplyReceipt>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "Unable to prepare Scene replacement"));
    }
}

foundation::Result<SceneApplyReceipt> Scene::apply(CompiledSceneDelta delta) {
    if (delta.beforeRevision != _revision || delta.afterRevision <= delta.beforeRevision) {
        return foundation::Result<SceneApplyReceipt>::failure(makeError(
            foundation::ErrorCode::kInvalidRevision, "Delta does not advance the current Scene"));
    }

    auto recordsResult = _records.prepareApply(delta.mutations);
    if (!recordsResult) {
        return foundation::Result<SceneApplyReceipt>::failure(recordsResult.error());
    }
    SceneRecordStore::PreparedUpdate preparedRecords = std::move(recordsResult.value());

    try {
        std::vector<SpatialMutation> spatialMutations = makeSpatialMutations(delta.mutations);
        auto renderResult =
            _renderScene->prepareApply(delta.mutations, delta.beforeRevision, delta.afterRevision);
        if (!renderResult) {
            return foundation::Result<SceneApplyReceipt>::failure(renderResult.error());
        }
        if (!renderResult.value()) {
            return foundation::Result<SceneApplyReceipt>::failure(
                makeError(foundation::ErrorCode::kParticipantRejected,
                          "Render participant returned an empty prepared update"));
        }

        auto spatialResult = _spatialIndex->prepareApply(
            spatialMutations, delta.beforeRevision, delta.afterRevision);
        if (!spatialResult) {
            return foundation::Result<SceneApplyReceipt>::failure(spatialResult.error());
        }
        if (!spatialResult.value()) {
            return foundation::Result<SceneApplyReceipt>::failure(
                makeError(foundation::ErrorCode::kParticipantRejected,
                          "Spatial participant returned an empty prepared update"));
        }

        auto damageResult = _damageTracker.prepareApply(delta);
        if (!damageResult) {
            return foundation::Result<SceneApplyReceipt>::failure(damageResult.error());
        }
        SceneApplyReceipt receipt{
            .beforeRevision = delta.beforeRevision,
            .afterRevision = delta.afterRevision,
            .recordsTouched = delta.mutations.size(),
            .renderNodesTouched = delta.mutations.size(),
            .spatialRecordsTouched = delta.mutations.size(),
            .damage = damageForDelta(delta),
        };

        _commitDiagnostics = SceneCommitDiagnostics{
            .transactionCount = _commitDiagnostics.transactionCount + 1,
        };
        std::uint8_t stage = 0;
        _records.commit(std::move(preparedRecords));
        _commitDiagnostics.recordStoreStage = ++stage;
        _renderScene->commit(std::move(renderResult.value()));
        _commitDiagnostics.renderSceneStage = ++stage;
        _spatialIndex->commit(std::move(spatialResult.value()));
        _commitDiagnostics.spatialIndexStage = ++stage;
        _damageTracker.commit(std::move(damageResult.value()));
        _commitDiagnostics.damageStage = ++stage;
        _revision = delta.afterRevision;
        _commitDiagnostics.revisionStage = ++stage;
        return foundation::Result<SceneApplyReceipt>::success(std::move(receipt));
    } catch (const std::bad_alloc&) {
        return foundation::Result<SceneApplyReceipt>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "Unable to prepare Scene delta"));
    }
}

foundation::Result<SceneQueryResult> Scene::query(const SceneQuery& request) const {
    if (!request.worldRect.isFiniteAndOrdered()) {
        return foundation::Result<SceneQueryResult>::failure(
            makeError(foundation::ErrorCode::kInvalidArgument, "Scene query bounds are invalid"));
    }
    auto spatialResult = _spatialIndex->query(request.worldRect);
    if (!spatialResult) {
        return foundation::Result<SceneQueryResult>::failure(spatialResult.error());
    }
    try {
        SceneQueryResult result{
            .revision = _revision,
            .backToFront = {},
            .diagnostics =
                SceneQueryDiagnostics{
                    .candidatesExamined = spatialResult.value().examinedRecords,
                },
        };
        result.backToFront.reserve(spatialResult.value().candidates.size());
        for (ObjectId objectId : spatialResult.value().candidates) {
            const SceneRecord* record = _records.find(objectId);
            if (record != nullptr && isVisible(*record) &&
                record->worldBounds.intersects(request.worldRect)) {
                result.backToFront.push_back(objectId);
            }
        }
        std::sort(result.backToFront.begin(),
                  result.backToFront.end(),
                  [this](ObjectId left, ObjectId right) {
                      const SceneRecord* leftRecord = _records.find(left);
                      const SceneRecord* rightRecord = _records.find(right);
                      return leftRecord->orderKey < rightRecord->orderKey ||
                             (leftRecord->orderKey == rightRecord->orderKey && left < right);
                  });
        result.backToFront.erase(std::unique(result.backToFront.begin(), result.backToFront.end()),
                                 result.backToFront.end());
        result.diagnostics.visibleRecords = result.backToFront.size();
        return foundation::Result<SceneQueryResult>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return foundation::Result<SceneQueryResult>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "Scene query could not allocate"));
    }
}

foundation::Result<HitTestResult> Scene::hitTest(const HitTestRequest& request) const {
    if (!std::isfinite(request.worldPoint.x) || !std::isfinite(request.worldPoint.y) ||
        !std::isfinite(request.tolerance) || request.tolerance < 0.0F ||
        request.maximumResults == 0U || !hasKnownHitKinds(request.filter.kinds)) {
        return foundation::Result<HitTestResult>::failure(
            makeError(foundation::ErrorCode::kInvalidArgument, "HitTest request is invalid"));
    }
    const float epsilon = std::numeric_limits<float>::epsilon() *
                          std::max({1.0F,
                                    std::fabs(request.worldPoint.x),
                                    std::fabs(request.worldPoint.y),
                                    request.tolerance});
    const float radius = request.tolerance + epsilon;
    const WorldRect coarseRect{
        request.worldPoint.x - radius,
        request.worldPoint.y - radius,
        request.worldPoint.x + radius,
        request.worldPoint.y + radius,
    };
    if (!coarseRect.isFiniteAndOrdered()) {
        return foundation::Result<HitTestResult>::failure(
            makeError(foundation::ErrorCode::kInvalidArgument, "HitTest query bounds overflow"));
    }
    auto spatialResult = _spatialIndex->query(coarseRect);
    if (!spatialResult) {
        return foundation::Result<HitTestResult>::failure(spatialResult.error());
    }
    try {
        HitTestResult result{
            .revision = _revision,
            .frontToBack = {},
            .diagnostics = {},
        };
        result.diagnostics.candidatesExamined = spatialResult.value().examinedRecords;
        std::vector<ObjectId> candidates;
        candidates.reserve(spatialResult.value().candidates.size());
        std::unordered_set<ObjectId, foundation::ObjectIdHash> seen;
        seen.reserve(spatialResult.value().candidates.size());
        for (ObjectId objectId : spatialResult.value().candidates) {
            if (!seen.insert(objectId).second) {
                continue;
            }
            const SceneRecord* record = _records.find(objectId);
            if (record == nullptr || !hasFlag(record->flags, SceneRecordFlags::kVisible) ||
                !hasFlag(record->flags, SceneRecordFlags::kHitTestable) ||
                !record->worldBounds.intersects(coarseRect) ||
                (!request.filter.includeLocked &&
                 hasFlag(record->flags, SceneRecordFlags::kLocked)) ||
                (kindBit(record->kind) & static_cast<std::uint32_t>(request.filter.kinds)) == 0U) {
                continue;
            }
            candidates.push_back(objectId);
        }
        result.diagnostics.candidatesReturned = spatialResult.value().candidates.size();
        result.diagnostics.candidatesAfterFilter = candidates.size();
        std::sort(candidates.begin(), candidates.end(), [this](ObjectId left, ObjectId right) {
            const SceneRecord* leftRecord = _records.find(left);
            const SceneRecord* rightRecord = _records.find(right);
            return rightRecord->orderKey < leftRecord->orderKey ||
                   (rightRecord->orderKey == leftRecord->orderKey && right < left);
        });
        for (ObjectId objectId : candidates) {
            ++result.diagnostics.preciseTests;
            auto precise = _renderScene->preciseHitTest(
                PreciseHitRequest{objectId, request.worldPoint, request.tolerance});
            if (!precise) {
                return foundation::Result<HitTestResult>::failure(precise.error());
            }
            if (!precise.value().hit) {
                continue;
            }
            result.frontToBack.push_back(objectId);
            ++result.diagnostics.preciseHits;
            if (result.frontToBack.size() >= request.maximumResults) {
                break;
            }
        }
        return foundation::Result<HitTestResult>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return foundation::Result<HitTestResult>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "HitTest could not allocate"));
    }
}

foundation::Result<SceneDrawList> Scene::buildDrawList(const SceneQueryResult& visible) const {
    if (visible.revision != _revision) {
        return foundation::Result<SceneDrawList>::failure(
            makeError(foundation::ErrorCode::kInvalidRevision, "Scene query result is stale"));
    }
    return _renderScene->buildDrawList(visible.backToFront);
}

foundation::Result<SceneFrameInput> Scene::buildFrame(const SceneQuery& request,
                                                       SceneRevision afterExclusive) const {
    if (afterExclusive > _revision) {
        return foundation::Result<SceneFrameInput>::failure(makeError(
            foundation::ErrorCode::kInvalidRevision,
            "Frame damage revision cannot be newer than the current Scene"));
    }
    auto queryResult = query(request);
    if (!queryResult) {
        return foundation::Result<SceneFrameInput>::failure(queryResult.error());
    }
    auto drawListResult = buildDrawList(queryResult.value());
    if (!drawListResult) {
        return foundation::Result<SceneFrameInput>::failure(drawListResult.error());
    }
    SceneFrameInput frame{
        .revision = _revision,
        .damage = collectDamage(afterExclusive, _revision),
        .query = std::move(queryResult.value()),
        .drawList = std::move(drawListResult.value()),
    };
    return foundation::Result<SceneFrameInput>::success(std::move(frame));
}

DamageSet Scene::collectDamage(SceneRevision afterExclusive, SceneRevision throughInclusive) const {
    return _damageTracker.collect(afterExclusive, throughInclusive);
}

void Scene::compactDamageThrough(SceneRevision revision) {
    _damageTracker.compactThrough(revision);
}

} // namespace canvas
