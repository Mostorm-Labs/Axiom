#include "canvas/poc03/rf01/poc03_scene_source.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace canvas::poc03::rf01 {
namespace {

foundation::Error makeError(foundation::ErrorCode code, const char* message) {
    return foundation::Error{code, message};
}

WorldRect toWorldRect(const Bounds& bounds) {
    return WorldRect{bounds.left, bounds.top, bounds.right, bounds.bottom};
}

SceneObjectKind toSceneKind(NodeType type) {
    switch (type) {
    case NodeType::kShape:
        return SceneObjectKind::kShape;
    case NodeType::kImage:
        return SceneObjectKind::kImage;
    case NodeType::kVectorPath:
        return SceneObjectKind::kVectorPath;
    case NodeType::kSimpleText:
        return SceneObjectKind::kRichText;
    case NodeType::kStroke:
        return SceneObjectKind::kVectorStroke;
    }
    return static_cast<SceneObjectKind>(0);
}

SceneRecordFlags flagsFor(const NodeRecord& record) {
    std::uint32_t flags = static_cast<std::uint32_t>(SceneRecordFlags::kVisible) |
                          static_cast<std::uint32_t>(SceneRecordFlags::kHitTestable);
    if (record.locked) {
        flags |= static_cast<std::uint32_t>(SceneRecordFlags::kLocked);
    }
    return static_cast<SceneRecordFlags>(flags);
}

SceneRecord toSceneRecord(const NodeRecord& record, std::uint32_t slot) {
    const std::uint32_t generation = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
        record.content_revision, 1U, std::numeric_limits<std::uint32_t>::max()));
    return SceneRecord{
        .objectId = ObjectId::fromUint64(record.id),
        .orderKey = SceneOrderKey(record.order),
        .kind = toSceneKind(record.type),
        .flags = flagsFor(record),
        .worldBounds = toWorldRect(record.bounds),
        .contentRevision = ContentRevision(record.content_revision),
        .renderPayload = RenderPayloadRef{slot, generation},
        .hitGeometry = HitGeometryRef{slot, generation},
    };
}

bool matches(const SceneRecord& sceneRecord, const NodeRecord& source, std::uint32_t slot) {
    return sceneRecord == toSceneRecord(source, slot);
}

SceneMutationKind toMutationKind(OperationKind kind) {
    switch (kind) {
    case OperationKind::kCreate:
        return SceneMutationKind::kInsert;
    case OperationKind::kUpdate:
    case OperationKind::kReorder:
        return SceneMutationKind::kUpdate;
    case OperationKind::kDelete:
        return SceneMutationKind::kRemove;
    }
    return static_cast<SceneMutationKind>(255U);
}

struct PixelBuffer final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> bytes;

    void set(std::int32_t x, std::int32_t y, std::uint32_t argb) {
        if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(width) ||
            y >= static_cast<std::int32_t>(height)) {
            return;
        }
        const std::size_t offset =
            (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4U;
        bytes[offset] = static_cast<std::uint8_t>(argb >> 16U);
        bytes[offset + 1U] = static_cast<std::uint8_t>(argb >> 8U);
        bytes[offset + 2U] = static_cast<std::uint8_t>(argb);
        bytes[offset + 3U] = static_cast<std::uint8_t>(argb >> 24U);
    }
};

struct PixelRect final {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

PixelRect toPixelRect(const Bounds& bounds, const ViewState& view) {
    const float scale = view.zoom * view.dpr;
    return PixelRect{
        static_cast<std::int32_t>(std::floor((bounds.left - view.world_viewport.left) * scale)),
        static_cast<std::int32_t>(std::floor((bounds.top - view.world_viewport.top) * scale)),
        static_cast<std::int32_t>(std::ceil((bounds.right - view.world_viewport.left) * scale)),
        static_cast<std::int32_t>(std::ceil((bounds.bottom - view.world_viewport.top) * scale)),
    };
}

void fillRect(PixelBuffer& pixels, const PixelRect& rect, std::uint32_t color) {
    for (std::int32_t y = rect.top; y < rect.bottom; ++y) {
        for (std::int32_t x = rect.left; x < rect.right; ++x) {
            pixels.set(x, y, color);
        }
    }
}

void drawLine(PixelBuffer& pixels,
              std::int32_t x0,
              std::int32_t y0,
              std::int32_t x1,
              std::int32_t y1,
              std::uint32_t color) {
    const std::int32_t dx = std::abs(x1 - x0);
    const std::int32_t sx = x0 < x1 ? 1 : -1;
    const std::int32_t dy = -std::abs(y1 - y0);
    const std::int32_t sy = y0 < y1 ? 1 : -1;
    std::int32_t error = dx + dy;
    while (true) {
        pixels.set(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const std::int32_t doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void strokeRect(PixelBuffer& pixels, const PixelRect& rect, std::uint32_t color) {
    if (rect.left >= rect.right || rect.top >= rect.bottom) {
        return;
    }
    drawLine(pixels, rect.left, rect.top, rect.right - 1, rect.top, color);
    drawLine(pixels, rect.right - 1, rect.top, rect.right - 1, rect.bottom - 1, color);
    drawLine(pixels, rect.right - 1, rect.bottom - 1, rect.left, rect.bottom - 1, color);
    drawLine(pixels, rect.left, rect.bottom - 1, rect.left, rect.top, color);
}

void drawRecord(PixelBuffer& pixels, const NodeRecord& record, const ViewState& view) {
    const PixelRect rect = toPixelRect(record.bounds, view);
    switch (record.type) {
    case NodeType::kShape:
        fillRect(pixels, rect, record.rgba);
        break;
    case NodeType::kImage:
        fillRect(pixels, rect, record.rgba);
        drawLine(pixels, rect.left, rect.top, rect.right - 1, rect.bottom - 1, 0xffffffffU);
        break;
    case NodeType::kVectorPath:
        drawLine(pixels, rect.left, rect.bottom - 1, rect.right - 1, rect.top, record.rgba);
        break;
    case NodeType::kSimpleText:
        strokeRect(pixels, rect, record.rgba);
        drawLine(pixels,
                 (rect.left + rect.right) / 2,
                 rect.top,
                 (rect.left + rect.right) / 2,
                 rect.bottom - 1,
                 record.rgba);
        break;
    case NodeType::kStroke:
        drawLine(pixels, rect.left, rect.top, rect.right - 1, rect.bottom - 1, record.rgba);
        break;
    }
}

foundation::Result<PixelBuffer> makePixelBuffer(const ViewState& view) {
    if (view.pixel_width == 0 || view.pixel_height == 0 ||
        !view.world_viewport.IsFiniteAndOrdered() || !std::isfinite(view.zoom) ||
        view.zoom <= 0.0F || !std::isfinite(view.dpr) || view.dpr <= 0.0F) {
        return foundation::Result<PixelBuffer>::failure(
            makeError(foundation::ErrorCode::kInvalidArgument, "Reference RGBA view is invalid"));
    }
    try {
        PixelBuffer pixels{
            .width = view.pixel_width,
            .height = view.pixel_height,
        };
        pixels.bytes.resize(static_cast<std::size_t>(pixels.width) * pixels.height * 4U);
        for (std::size_t offset = 0; offset < pixels.bytes.size(); offset += 4U) {
            pixels.bytes[offset] = 244U;
            pixels.bytes[offset + 1U] = 245U;
            pixels.bytes[offset + 2U] = 247U;
            pixels.bytes[offset + 3U] = 255U;
        }
        return foundation::Result<PixelBuffer>::success(std::move(pixels));
    } catch (const std::bad_alloc&) {
        return foundation::Result<PixelBuffer>::failure(makeError(
            foundation::ErrorCode::kOutOfMemory, "Reference RGBA buffer allocation failed"));
    }
}

} // namespace

foundation::Result<std::uint32_t> Poc03SceneSource::slotFor(std::uint64_t objectId) const {
    const auto found = _slots.find(objectId);
    if (found != _slots.end()) {
        return foundation::Result<std::uint32_t>::success(found->second);
    }
    if (_nextSlot > std::numeric_limits<std::uint32_t>::max()) {
        return foundation::Result<std::uint32_t>::failure(makeError(
            foundation::ErrorCode::kOutOfMemory, "POC-03 payload slot capacity is exhausted"));
    }
    try {
        const auto slot = static_cast<std::uint32_t>(_nextSlot++);
        _slots.emplace(objectId, slot);
        return foundation::Result<std::uint32_t>::success(slot);
    } catch (const std::bad_alloc&) {
        return foundation::Result<std::uint32_t>::failure(makeError(
            foundation::ErrorCode::kOutOfMemory, "POC-03 payload slot allocation failed"));
    }
}

foundation::Result<CompiledSceneSnapshot> Poc03SceneSource::compileFull() const {
    if (_document == nullptr) {
        return foundation::Result<CompiledSceneSnapshot>::failure(makeError(
            foundation::ErrorCode::kInvalidArgument, "POC-03 Scene source has no bound Document"));
    }
    return compileFull(*_document);
}

foundation::Result<CompiledSceneDelta> Poc03SceneSource::compileDelta() const {
    if (_document == nullptr || _changes == nullptr) {
        return foundation::Result<CompiledSceneDelta>::failure(
            makeError(foundation::ErrorCode::kRequiresFullRebuild,
                      "POC-03 Scene source has no bound ChangeSet"));
    }
    return compileDelta(*_document, *_changes);
}

foundation::Result<CompiledSceneSnapshot>
Poc03SceneSource::compileFull(const Document& document) const {
    if (document.revision() == 0) {
        return foundation::Result<CompiledSceneSnapshot>::failure(
            makeError(foundation::ErrorCode::kInvalidRevision,
                      "POC-03 Document must have a committed revision"));
    }
    try {
        CompiledSceneSnapshot snapshot{
            .sourceRevision = SceneRevision(document.revision()),
        };
        const std::vector<const NodeRecord*> records = document.OrderedRecords();
        snapshot.records.reserve(records.size());
        for (const NodeRecord* record : records) {
            auto slotResult = slotFor(record->id);
            if (!slotResult) {
                return foundation::Result<CompiledSceneSnapshot>::failure(slotResult.error());
            }
            snapshot.records.push_back(toSceneRecord(*record, slotResult.value()));
        }
        return foundation::Result<CompiledSceneSnapshot>::success(std::move(snapshot));
    } catch (const std::bad_alloc&) {
        return foundation::Result<CompiledSceneSnapshot>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "POC-03 snapshot allocation failed"));
    }
}

foundation::Result<CompiledSceneDelta>
Poc03SceneSource::compileDelta(const Document& document, const ChangeSet& changes) const {
    if (changes.before_revision == std::numeric_limits<std::uint64_t>::max() ||
        changes.before_revision + 1U != changes.after_revision ||
        changes.after_revision != document.revision() || changes.semantic_changes.empty()) {
        return foundation::Result<CompiledSceneDelta>::failure(
            makeError(foundation::ErrorCode::kRequiresFullRebuild,
                      "POC-03 ChangeSet revision or semantic changes are invalid"));
    }
    try {
        CompiledSceneDelta delta{
            .beforeRevision = SceneRevision(changes.before_revision),
            .afterRevision = SceneRevision(changes.after_revision),
        };
        delta.mutations.reserve(changes.semantic_changes.size());
        for (const SemanticChange& change : changes.semantic_changes) {
            const auto kindValue = static_cast<std::uint8_t>(change.kind);
            if (change.id == 0U || kindValue > static_cast<std::uint8_t>(OperationKind::kReorder)) {
                return foundation::Result<CompiledSceneDelta>::failure(
                    makeError(foundation::ErrorCode::kRequiresFullRebuild,
                              "POC-03 ChangeSet contains an unknown semantic change"));
            }
            const NodeRecord* authoritative = document.Find(change.id);
            const bool shapeMatches =
                change.kind == OperationKind::kCreate
                    ? !change.before && change.after
                    : (change.kind == OperationKind::kDelete ? change.before && !change.after
                                                             : change.before && change.after);
            const bool afterMatches =
                change.kind == OperationKind::kDelete
                    ? authoritative == nullptr && !change.after
                    : authoritative != nullptr && change.after && *authoritative == *change.after;
            const bool idsMatch = (!change.before || change.before->id == change.id) &&
                                  (!change.after || change.after->id == change.id);
            if (!shapeMatches || !afterMatches || !idsMatch) {
                return foundation::Result<CompiledSceneDelta>::failure(
                    makeError(foundation::ErrorCode::kRequiresFullRebuild,
                              "POC-03 ChangeSet differs from the authoritative Document"));
            }
            auto slotResult = slotFor(change.id);
            if (!slotResult) {
                return foundation::Result<CompiledSceneDelta>::failure(slotResult.error());
            }
            SceneMutation mutation{
                .kind = toMutationKind(change.kind),
                .objectId = ObjectId::fromUint64(change.id),
            };
            if (change.before) {
                mutation.before = toSceneRecord(*change.before, slotResult.value());
            }
            if (change.after) {
                mutation.after = toSceneRecord(*change.after, slotResult.value());
            }
            delta.mutations.push_back(std::move(mutation));
        }
        if (changes.hints && changes.hints->before_revision == changes.before_revision &&
            changes.hints->after_revision == changes.after_revision && changes.hints->world_dirty &&
            changes.hints->world_dirty->IsFiniteAndOrdered()) {
            delta.hints = canvas::InvalidationHints{
                .worldDirty = toWorldRect(*changes.hints->world_dirty),
            };
            const bool orderChanged = std::any_of(changes.semantic_changes.begin(),
                                                  changes.semantic_changes.end(),
                                                  [](const SemanticChange& change) {
                                                      return change.kind == OperationKind::kReorder;
                                                  });
            if (orderChanged) {
                delta.hints->flags = InvalidationHintFlags::kOrderChanged;
            }
        }
        return foundation::Result<CompiledSceneDelta>::success(std::move(delta));
    } catch (const std::bad_alloc&) {
        return foundation::Result<CompiledSceneDelta>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "POC-03 delta allocation failed"));
    }
}

foundation::Result<std::string> Poc03SceneSource::projectedDigest(SceneReadView scene,
                                                                  const Document& document) const {
    if (scene.revision().value() != document.revision() ||
        scene.records().size() != document.active_count()) {
        return foundation::Result<std::string>::failure(makeError(
            foundation::ErrorCode::kInvalidRevision, "Scene and POC-03 Document revisions differ"));
    }
    const std::vector<const NodeRecord*> records = document.OrderedRecords();
    for (std::size_t index = 0; index < records.size(); ++index) {
        auto slotResult = slotFor(records[index]->id);
        if (!slotResult || !matches(scene.records()[index], *records[index], slotResult.value())) {
            return foundation::Result<std::string>::failure(
                makeError(foundation::ErrorCode::kInvalidRecord,
                          "Scene projection differs from POC-03 Document"));
        }
    }
    return foundation::Result<std::string>::success(SceneCompiler().CompileFull(document).Digest());
}

foundation::Result<std::vector<std::uint8_t>>
Poc03SceneSource::referenceRgba(const RuntimeScene& scene, const ViewState& view) const {
    auto pixelsResult = makePixelBuffer(view);
    if (!pixelsResult) {
        return foundation::Result<std::vector<std::uint8_t>>::failure(pixelsResult.error());
    }
    PixelBuffer pixels = std::move(pixelsResult.value());
    const ViewQueryResult query = QueryView(scene, view, std::nullopt);
    for (std::uint32_t slot : query.visible) {
        const std::optional<NodeRecord> record = scene.RecordAt(slot);
        if (record) {
            drawRecord(pixels, *record, view);
        }
    }
    return foundation::Result<std::vector<std::uint8_t>>::success(std::move(pixels.bytes));
}

foundation::Result<std::vector<std::uint8_t>> Poc03SceneSource::referenceRgba(
    const Scene& scene, const Document& document, const ViewState& view) const {
    auto pixelsResult = makePixelBuffer(view);
    if (!pixelsResult) {
        return foundation::Result<std::vector<std::uint8_t>>::failure(pixelsResult.error());
    }
    const auto queryResult = scene.query(SceneQuery{toWorldRect(view.world_viewport)});
    if (!queryResult) {
        return foundation::Result<std::vector<std::uint8_t>>::failure(queryResult.error());
    }
    PixelBuffer pixels = std::move(pixelsResult.value());
    for (ObjectId objectId : queryResult.value().backToFront) {
        std::uint64_t sourceId = 0;
        for (std::size_t index = 0; index < sizeof(sourceId); ++index) {
            sourceId |= static_cast<std::uint64_t>(objectId.bytes[index]) << (index * 8U);
        }
        const NodeRecord* record = document.Find(sourceId);
        if (record == nullptr) {
            return foundation::Result<std::vector<std::uint8_t>>::failure(
                makeError(foundation::ErrorCode::kMissingObject,
                          "RF-01 draw list references a missing POC-03 node"));
        }
        drawRecord(pixels, *record, view);
    }
    return foundation::Result<std::vector<std::uint8_t>>::success(std::move(pixels.bytes));
}

} // namespace canvas::poc03::rf01
