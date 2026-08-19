#include "canvas/scene/uniform_grid_spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace canvas {
namespace {

struct PreparedGridUpdate final : public IPreparedSpatialUpdate {
    SceneRevision revision;
    std::vector<SpatialRecord> records;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> cells;
};

foundation::Error makeError(foundation::ErrorCode code, const char* message) {
    return foundation::Error{code, message};
}

std::int64_t cellKey(std::int32_t x, std::int32_t y) {
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
        static_cast<std::uint32_t>(y));
}

foundation::Result<std::vector<std::int64_t>> cellsFor(const WorldRect& bounds, float cellSize) {
    if (!bounds.isFiniteAndOrdered()) {
        return foundation::Result<std::vector<std::int64_t>>::failure(
            makeError(foundation::ErrorCode::kInvalidArgument, "Spatial bounds are invalid"));
    }
    const double firstXValue = std::floor(bounds.left / cellSize);
    const double firstYValue = std::floor(bounds.top / cellSize);
    const float lastXCoordinate =
        bounds.right > bounds.left
            ? std::nextafter(bounds.right, -std::numeric_limits<float>::infinity())
            : bounds.right;
    const float lastYCoordinate =
        bounds.bottom > bounds.top
            ? std::nextafter(bounds.bottom, -std::numeric_limits<float>::infinity())
            : bounds.bottom;
    const double lastXValue = std::floor(lastXCoordinate / cellSize);
    const double lastYValue = std::floor(lastYCoordinate / cellSize);
    if (firstXValue < std::numeric_limits<std::int32_t>::min() ||
        lastXValue > std::numeric_limits<std::int32_t>::max() ||
        firstYValue < std::numeric_limits<std::int32_t>::min() ||
        lastYValue > std::numeric_limits<std::int32_t>::max()) {
        return foundation::Result<std::vector<std::int64_t>>::failure(
            makeError(foundation::ErrorCode::kInvalidArgument, "Spatial cell overflow"));
    }
    const std::int32_t firstX = static_cast<std::int32_t>(firstXValue);
    const std::int32_t firstY = static_cast<std::int32_t>(firstYValue);
    const std::int32_t lastX = static_cast<std::int32_t>(lastXValue);
    const std::int32_t lastY = static_cast<std::int32_t>(lastYValue);
    const std::uint64_t width = static_cast<std::uint64_t>(static_cast<std::int64_t>(lastX) -
                                                           static_cast<std::int64_t>(firstX) + 1);
    const std::uint64_t height = static_cast<std::uint64_t>(static_cast<std::int64_t>(lastY) -
                                                            static_cast<std::int64_t>(firstY) + 1);
    if (width > 65536U || height > 65536U || width * height > 1048576U) {
        return foundation::Result<std::vector<std::int64_t>>::failure(makeError(
            foundation::ErrorCode::kInvalidArgument, "Spatial record covers too many cells"));
    }
    try {
        std::vector<std::int64_t> result;
        result.reserve(static_cast<std::size_t>(width * height));
        for (std::int64_t y = firstY; y <= lastY; ++y) {
            for (std::int64_t x = firstX; x <= lastX; ++x) {
                result.push_back(
                    cellKey(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)));
            }
        }
        return foundation::Result<std::vector<std::int64_t>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::vector<std::int64_t>>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "Unable to enumerate spatial cells"));
    }
}

} // namespace

UniformGridSpatialIndex::UniformGridSpatialIndex(float cellSize) : _cellSize(cellSize) {
    if (!std::isfinite(cellSize) || cellSize <= 0.0F) {
        throw std::invalid_argument("spatial cell size must be finite and positive");
    }
}

foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>
UniformGridSpatialIndex::prepareReplace(std::span<const SpatialRecord> records,
                                        SceneRevision revision) const {
    ++_prepareCount;
    try {
        auto update = std::make_unique<PreparedGridUpdate>();
        update->revision = revision;
        update->records.assign(records.begin(), records.end());
        for (std::size_t index = 0; index < records.size(); ++index) {
            auto cellsResult = cellsFor(records[index].worldBounds, _cellSize);
            if (!cellsResult) {
                return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::failure(
                    cellsResult.error());
            }
            for (std::int64_t key : cellsResult.value()) {
                update->cells[key].push_back(static_cast<std::uint32_t>(index));
            }
        }
        return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::success(
            std::move(update));
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory,
                      "UniformGridSpatialIndex could not prepare replacement"));
    }
}

foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>> UniformGridSpatialIndex::prepareApply(
    std::span<const SpatialMutation>, SceneRevision, SceneRevision) const {
    ++_prepareCount;
    return foundation::Result<std::unique_ptr<IPreparedSpatialUpdate>>::failure(
        makeError(foundation::ErrorCode::kRequiresFullRebuild,
                  "UniformGridSpatialIndex delta support begins in RF01-2"));
}

void UniformGridSpatialIndex::commit(std::unique_ptr<IPreparedSpatialUpdate> prepared) noexcept {
    auto* update = static_cast<PreparedGridUpdate*>(prepared.get());
    _records.swap(update->records);
    _cells.swap(update->cells);
    _revision = update->revision;
    ++_commitCount;
}

foundation::Result<SpatialQueryResult>
UniformGridSpatialIndex::query(const WorldRect& worldRect) const {
    auto cellsResult = cellsFor(worldRect, _cellSize);
    if (!cellsResult) {
        return foundation::Result<SpatialQueryResult>::failure(cellsResult.error());
    }
    try {
        SpatialQueryResult result;
        std::unordered_set<std::uint32_t> visited;
        visited.reserve(_records.size());
        _lastCellVisits = cellsResult.value().size();
        for (std::int64_t key : cellsResult.value()) {
            const auto found = _cells.find(key);
            if (found == _cells.end()) {
                continue;
            }
            for (std::uint32_t index : found->second) {
                if (!visited.insert(index).second) {
                    continue;
                }
                ++result.examinedRecords;
                if (_records[index].worldBounds.intersects(worldRect)) {
                    result.candidates.push_back(_records[index].objectId);
                }
            }
        }
        _lastExaminedRecords = result.examinedRecords;
        _lastReturnedCandidates = result.candidates.size();
        return foundation::Result<SpatialQueryResult>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return foundation::Result<SpatialQueryResult>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "Spatial query could not allocate"));
    }
}

SpatialIndexDiagnostics UniformGridSpatialIndex::diagnostics() const {
    std::size_t estimatedBytes = sizeof(*this) + _records.capacity() * sizeof(SpatialRecord);
    for (const auto& [key, values] : _cells) {
        static_cast<void>(key);
        estimatedBytes += sizeof(std::int64_t) + 32U + values.capacity() * sizeof(std::uint32_t);
    }
    return SpatialIndexDiagnostics{
        .revision = _revision,
        .recordCount = _records.size(),
        .prepareCount = _prepareCount,
        .commitCount = _commitCount,
        .lastExaminedRecords = _lastExaminedRecords,
        .lastReturnedCandidates = _lastReturnedCandidates,
        .lastCellVisits = _lastCellVisits,
        .estimatedBytes = estimatedBytes,
    };
}

} // namespace canvas
