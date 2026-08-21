#pragma once

#include "canvas/foundation/object_id.hpp"
#include "canvas/foundation/revision.hpp"
#include "canvas/foundation/stable_order_key.hpp"
#include "canvas/foundation/world_geometry.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace canvas {

using foundation::ContentRevision;
using foundation::ObjectId;
using foundation::SceneRevision;
using foundation::WorldPoint;
using foundation::WorldRect;

using SceneOrderKey = foundation::StableOrderKey;

enum class SceneObjectKind : std::uint8_t {
    kShape = 1,
    kImage = 2,
    kVectorPath = 3,
    kRichText = 4,
    kVectorStroke = 5,
    kDabStroke = 6,
};

enum class SceneRecordFlags : std::uint32_t {
    kNone = 0,
    kVisible = 1U << 0U,
    kLocked = 1U << 1U,
    kHitTestable = 1U << 2U,
};

struct RenderPayloadRef final {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;

    bool operator==(const RenderPayloadRef&) const = default;
};

struct HitGeometryRef final {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;

    bool operator==(const HitGeometryRef&) const = default;
};

enum class InvalidationHintFlags : std::uint32_t {
    kNone = 0,
    kLayoutChanged = 1U << 0U,
    kResourceChanged = 1U << 1U,
    kOrderChanged = 1U << 2U,
};

struct InvalidationHints final {
    std::optional<WorldRect> worldDirty;
    InvalidationHintFlags flags = InvalidationHintFlags::kNone;
    // A hint is trusted only when both stamps match the delta that carries it.
    std::optional<SceneRevision> beforeRevision;
    std::optional<SceneRevision> afterRevision;
};

struct SceneRecord final {
    ObjectId objectId;
    SceneOrderKey orderKey;
    SceneObjectKind kind = SceneObjectKind::kShape;
    SceneRecordFlags flags = SceneRecordFlags::kNone;
    WorldRect worldBounds;
    ContentRevision contentRevision;
    RenderPayloadRef renderPayload;
    HitGeometryRef hitGeometry;

    bool operator==(const SceneRecord&) const = default;
};

enum class SceneMutationKind : std::uint8_t {
    kInsert,
    kUpdate,
    kRemove,
};

struct SceneMutation final {
    SceneMutationKind kind = SceneMutationKind::kInsert;
    ObjectId objectId;
    std::optional<SceneRecord> before;
    std::optional<SceneRecord> after;
};

struct CompiledSceneSnapshot final {
    SceneRevision sourceRevision;
    std::vector<SceneRecord> records;
};

struct CompiledSceneDelta final {
    SceneRevision beforeRevision;
    SceneRevision afterRevision;
    std::vector<SceneMutation> mutations;
    std::optional<InvalidationHints> hints;
};

} // namespace canvas
