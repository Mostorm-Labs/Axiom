#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/scene/scene_types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace canvas {

struct SpatialRecord final {
    ObjectId objectId;
    WorldRect worldBounds;

    bool operator==(const SpatialRecord&) const = default;
};

struct SpatialMutation final {
    SceneMutationKind kind = SceneMutationKind::kInsert;
    ObjectId objectId;
    std::optional<WorldRect> before;
    std::optional<WorldRect> after;
};

class IPreparedSpatialUpdate {
  public:
    virtual ~IPreparedSpatialUpdate() = default;
};

struct SpatialQueryResult final {
    std::vector<ObjectId> candidates;
    std::uint64_t examinedRecords = 0;
};

struct SpatialIndexDiagnostics final {
    SceneRevision revision;
    std::uint64_t recordCount = 0;
    std::uint64_t prepareCount = 0;
    std::uint64_t commitCount = 0;
    std::uint64_t lastExaminedRecords = 0;
    std::uint64_t lastReturnedCandidates = 0;
    std::uint64_t lastCellVisits = 0;
    std::size_t estimatedBytes = 0;
};

class ISpatialIndex {
  public:
    virtual ~ISpatialIndex() = default;

    virtual foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
    prepareReplace(std::span<const SpatialRecord> records, SceneRevision revision) const = 0;

    virtual foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
    prepareApply(std::span<const SpatialMutation> mutations,
                 SceneRevision beforeRevision,
                 SceneRevision afterRevision) const = 0;

    virtual void commit(std::unique_ptr<IPreparedSpatialUpdate> update) noexcept = 0;

    virtual foundation::Result<SpatialQueryResult> query(const WorldRect& worldRect) const = 0;
    [[nodiscard]] virtual SpatialIndexDiagnostics diagnostics() const = 0;
};

} // namespace canvas
