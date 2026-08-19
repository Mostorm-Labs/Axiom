#pragma once

#include "canvas/scene/spatial_index.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace canvas::testing {

class FakeSpatialIndex final : public ISpatialIndex {
  public:
    foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
    prepareReplace(std::span<const SpatialRecord> records, SceneRevision revision) const override;

    foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
    prepareApply(std::span<const SpatialMutation> mutations,
                 SceneRevision beforeRevision,
                 SceneRevision afterRevision) const override;

    void commit(std::unique_ptr<IPreparedSpatialUpdate> update) noexcept override;

    foundation::Result<SpatialQueryResult> query(const WorldRect& worldRect) const override;
    [[nodiscard]] SpatialIndexDiagnostics diagnostics() const override;

    void setRejectPrepare(bool reject) {
        _rejectPrepare = reject;
    }
    [[nodiscard]] std::uint64_t stateDigest() const;
    [[nodiscard]] std::span<const SpatialRecord> records() const {
        return _records;
    }

  private:
    bool _rejectPrepare = false;
    mutable std::uint64_t _prepareCount = 0;
    std::uint64_t _commitCount = 0;
    SceneRevision _revision;
    std::vector<SpatialRecord> _records;
};

} // namespace canvas::testing
