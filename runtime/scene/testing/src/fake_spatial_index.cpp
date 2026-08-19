#include "canvas/scene/testing/fake_spatial_index.hpp"

#include <algorithm>
#include <bit>
#include <new>
#include <utility>

namespace canvas::testing {
namespace {

class PreparedSpatialUpdate final : public IPreparedSpatialUpdate {
  public:
    PreparedSpatialUpdate(std::vector<SpatialRecord> records, SceneRevision revision)
        : records(std::move(records)), revision(revision) {}

    std::vector<SpatialRecord> records;
    SceneRevision revision;
};

foundation::Error rejectedError() {
    return foundation::Error{
        foundation::ErrorCode::kParticipantRejected,
        "Fake spatial participant rejected prepare",
    };
}

foundation::Error outOfMemoryError() {
    return foundation::Error{
        foundation::ErrorCode::kOutOfMemory,
        "Fake spatial participant could not prepare",
    };
}

void applyMutations(std::vector<SpatialRecord>& records,
                    std::span<const SpatialMutation> mutations) {
    for (const SpatialMutation& mutation : mutations) {
        auto found =
            std::find_if(records.begin(), records.end(), [&mutation](const SpatialRecord& record) {
                return record.objectId == mutation.objectId;
            });
        switch (mutation.kind) {
        case SceneMutationKind::kInsert:
            records.push_back(SpatialRecord{mutation.objectId, *mutation.after});
            break;
        case SceneMutationKind::kUpdate:
            found->worldBounds = *mutation.after;
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

void hashFloat(std::uint64_t& digest, float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        hashByte(digest, static_cast<std::uint8_t>(bits >> shift));
    }
}

} // namespace

foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
FakeSpatialIndex::prepareReplace(std::span<const SpatialRecord> records,
                                 SceneRevision revision) const {
    ++_prepareCount;
    if (_rejectPrepare) {
        return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::failure(
            rejectedError());
    }
    try {
        auto update = std::make_unique<PreparedSpatialUpdate>(
            std::vector<SpatialRecord>(records.begin(), records.end()), revision);
        return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::success(
            std::move(update));
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::failure(
            outOfMemoryError());
    }
}

foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
FakeSpatialIndex::prepareApply(std::span<const SpatialMutation> mutations,
                               SceneRevision beforeRevision,
                               SceneRevision afterRevision) const {
    ++_prepareCount;
    if (_rejectPrepare || beforeRevision != _revision) {
        return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::failure(
            rejectedError());
    }
    try {
        std::vector<SpatialRecord> records = _records;
        applyMutations(records, mutations);
        auto update = std::make_unique<PreparedSpatialUpdate>(std::move(records), afterRevision);
        return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::success(
            std::move(update));
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::failure(
            outOfMemoryError());
    }
}

void FakeSpatialIndex::commit(std::unique_ptr<IPreparedSpatialUpdate> preparedUpdate) noexcept {
    auto* update = static_cast<PreparedSpatialUpdate*>(preparedUpdate.get());
    _records.swap(update->records);
    _revision = update->revision;
    ++_commitCount;
}

foundation::Result<SpatialQueryResult> FakeSpatialIndex::query(const WorldRect& worldRect) const {
    SpatialQueryResult result;
    result.examinedRecords = _records.size();
    for (const SpatialRecord& record : _records) {
        if (record.worldBounds.intersects(worldRect)) {
            result.candidates.push_back(record.objectId);
        }
    }
    return foundation::Result<SpatialQueryResult>::success(std::move(result));
}

SpatialIndexDiagnostics FakeSpatialIndex::diagnostics() const {
    return SpatialIndexDiagnostics{
        .revision = _revision,
        .recordCount = _records.size(),
        .prepareCount = _prepareCount,
        .commitCount = _commitCount,
    };
}

std::uint64_t FakeSpatialIndex::stateDigest() const {
    std::uint64_t digest = 1469598103934665603ULL;
    hashUint64(digest, _revision.value());
    for (const SpatialRecord& record : _records) {
        for (std::uint8_t byte : record.objectId.bytes) {
            hashByte(digest, byte);
        }
        hashFloat(digest, record.worldBounds.left);
        hashFloat(digest, record.worldBounds.top);
        hashFloat(digest, record.worldBounds.right);
        hashFloat(digest, record.worldBounds.bottom);
    }
    return digest;
}

} // namespace canvas::testing
