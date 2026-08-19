#include "canvas/scene/testing/fake_render_scene.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace canvas::testing {
namespace {

class PreparedRenderUpdate final : public IPreparedRenderSceneUpdate {
  public:
    PreparedRenderUpdate(std::vector<SceneRecord> records, SceneRevision revision)
        : records(std::move(records)), revision(revision) {}

    std::vector<SceneRecord> records;
    SceneRevision revision;
};

foundation::Error rejectedError() {
    return foundation::Error{
        foundation::ErrorCode::kParticipantRejected,
        "Fake render participant rejected prepare",
    };
}

foundation::Error outOfMemoryError() {
    return foundation::Error{
        foundation::ErrorCode::kOutOfMemory,
        "Fake render participant could not prepare",
    };
}

void applyMutations(std::vector<SceneRecord>& records, std::span<const SceneMutation> mutations) {
    for (const SceneMutation& mutation : mutations) {
        auto found =
            std::find_if(records.begin(), records.end(), [&mutation](const SceneRecord& record) {
                return record.objectId == mutation.objectId;
            });
        switch (mutation.kind) {
        case SceneMutationKind::kInsert:
            records.push_back(*mutation.after);
            break;
        case SceneMutationKind::kUpdate:
            *found = *mutation.after;
            break;
        case SceneMutationKind::kRemove:
            records.erase(found);
            break;
        }
    }
}

void hashByte(std::uint64_t& digest, std::uint8_t byte) {
    digest ^= byte;
    digest *= 1099511628211ULL;
}

void hashUint64(std::uint64_t& digest, std::uint64_t value) {
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        hashByte(digest, static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
FakeRenderScene::prepareReplace(std::span<const SceneRecord> records,
                                SceneRevision revision) const {
    ++_prepareCount;
    if (_rejectPrepare) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            rejectedError());
    }
    try {
        auto update = std::make_unique<PreparedRenderUpdate>(
            std::vector<SceneRecord>(records.begin(), records.end()), revision);
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::success(
            std::move(update));
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            outOfMemoryError());
    }
}

foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>
FakeRenderScene::prepareApply(std::span<const SceneMutation> mutations,
                              SceneRevision beforeRevision,
                              SceneRevision afterRevision) const {
    ++_prepareCount;
    if (_rejectPrepare || beforeRevision != _revision) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            rejectedError());
    }
    try {
        std::vector<SceneRecord> records = _records;
        applyMutations(records, mutations);
        auto update = std::make_unique<PreparedRenderUpdate>(std::move(records), afterRevision);
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::success(
            std::move(update));
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedRenderSceneUpdate>>::failure(
            outOfMemoryError());
    }
}

void FakeRenderScene::commit(std::unique_ptr<IPreparedRenderSceneUpdate> preparedUpdate) noexcept {
    auto* update = static_cast<PreparedRenderUpdate*>(preparedUpdate.get());
    _records.swap(update->records);
    _revision = update->revision;
    ++_commitCount;
}

foundation::Result<PreciseHit>
FakeRenderScene::preciseHitTest(const PreciseHitRequest& request) const {
    const auto found =
        std::find_if(_records.begin(), _records.end(), [&request](const SceneRecord& record) {
            return record.objectId == request.objectId;
        });
    return foundation::Result<PreciseHit>::success(PreciseHit{found != _records.end(), 0.0F});
}

foundation::Result<SceneDrawList>
FakeRenderScene::buildDrawList(std::span<const ObjectId> backToFront) const {
    SceneDrawList drawList{.revision = _revision};
    drawList.items.reserve(backToFront.size());
    for (ObjectId objectId : backToFront) {
        const auto found =
            std::find_if(_records.begin(), _records.end(), [objectId](const SceneRecord& record) {
                return record.objectId == objectId;
            });
        if (found != _records.end()) {
            drawList.items.push_back(SceneDrawItem{
                found->objectId,
                found->orderKey,
                found->renderPayload,
            });
        }
    }
    return foundation::Result<SceneDrawList>::success(std::move(drawList));
}

RenderSceneDiagnostics FakeRenderScene::diagnostics() const {
    return RenderSceneDiagnostics{
        .revision = _revision,
        .nodeCount = _records.size(),
        .prepareCount = _prepareCount,
        .commitCount = _commitCount,
    };
}

std::uint64_t FakeRenderScene::stateDigest() const {
    std::uint64_t digest = 1469598103934665603ULL;
    hashUint64(digest, _revision.value());
    for (const SceneRecord& record : _records) {
        for (std::uint8_t byte : record.objectId.bytes) {
            hashByte(digest, byte);
        }
        hashUint64(digest, record.contentRevision.value());
        hashUint64(digest, record.orderKey.value());
    }
    return digest;
}

} // namespace canvas::testing
