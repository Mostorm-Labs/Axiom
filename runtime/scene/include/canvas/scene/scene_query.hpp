#pragma once

#include "canvas/scene/scene_types.hpp"

#include <cstddef>
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

enum class HitTestKindMask : std::uint32_t {
    kNone = 0,
    kShape = 1U << 0U,
    kImage = 1U << 1U,
    kVectorPath = 1U << 2U,
    kRichText = 1U << 3U,
    kVectorStroke = 1U << 4U,
    kDabStroke = 1U << 5U,
    kAll = 0x3f,
};

struct HitTestFilter final {
    HitTestKindMask kinds = HitTestKindMask::kAll;
    bool includeLocked = false;
};

struct HitTestRequest final {
    WorldPoint worldPoint;
    float tolerance = 0.0F;
    HitTestFilter filter;
    std::size_t maximumResults = 1U;
};

struct HitTestDiagnostics final {
    std::uint64_t candidatesExamined = 0;
    std::uint64_t candidatesReturned = 0;
    std::uint64_t candidatesAfterFilter = 0;
    std::uint64_t preciseTests = 0;
    std::uint64_t preciseHits = 0;
};

struct HitTestResult final {
    SceneRevision revision;
    std::vector<ObjectId> frontToBack;
    HitTestDiagnostics diagnostics;
};

} // namespace canvas
