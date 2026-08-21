#pragma once

#include "canvas/scene/spatial_index.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace canvas {

class UniformGridSpatialIndex final : public ISpatialIndex {
  public:
    explicit UniformGridSpatialIndex(float cellSize = 256.0F);

    foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
    prepareReplace(std::span<const SpatialRecord> records, SceneRevision revision) const override;
    foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
    prepareApply(std::span<const SpatialMutation> mutations,
                 SceneRevision beforeRevision,
                 SceneRevision afterRevision) const override;
    void commit(std::unique_ptr<IPreparedSpatialUpdate> update) noexcept override;
    foundation::Result<SpatialQueryResult> query(const WorldRect& worldRect) const override;
    [[nodiscard]] SpatialIndexDiagnostics diagnostics() const override;

  private:
    class PreparedGridUpdate;

    foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
    prepareRecords(std::vector<SpatialRecord> records, SceneRevision revision) const;

    float _cellSize;
    mutable std::uint64_t _prepareCount = 0;
    mutable std::uint64_t _lastExaminedRecords = 0;
    mutable std::uint64_t _lastReturnedCandidates = 0;
    mutable std::uint64_t _lastCellVisits = 0;
    std::uint64_t _commitCount = 0;
    SceneRevision _revision;
    std::vector<SpatialRecord> _records;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> _cells;
};

} // namespace canvas
