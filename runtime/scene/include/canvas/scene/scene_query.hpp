#pragma once

#include "canvas/scene/scene_types.hpp"

#include <cstdint>
#include <vector>

namespace canvas {

struct SceneQuery final {
    WorldRect worldRect;
};

struct SceneQueryDiagnostics final {
    std::uint64_t candidatesExamined = 0;
    std::uint64_t visibleRecords = 0;
};

struct SceneQueryResult final {
    SceneRevision revision;
    std::vector<ObjectId> backToFront;
    SceneQueryDiagnostics diagnostics;
};

} // namespace canvas
