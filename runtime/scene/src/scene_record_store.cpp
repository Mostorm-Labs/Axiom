#include "canvas/scene/scene_record_store.hpp"

#include <algorithm>
#include <new>
#include <unordered_set>
#include <utility>

namespace canvas {
namespace {

using foundation::Error;
using foundation::ErrorCode;

Error makeError(ErrorCode code, const char* message) {
    return Error{code, message};
}

bool recordLess(const SceneRecord& left, const SceneRecord& right) {
    if (left.orderKey != right.orderKey) {
        return left.orderKey < right.orderKey;
    }
    return left.objectId < right.objectId;
}

bool isKnownKind(SceneObjectKind kind) {
    switch (kind) {
    case SceneObjectKind::kShape:
    case SceneObjectKind::kImage:
    case SceneObjectKind::kVectorPath:
    case SceneObjectKind::kRichText:
    case SceneObjectKind::kVectorStroke:
    case SceneObjectKind::kDabStroke:
        return true;
    }
    return false;
}

foundation::Result<SceneRecord> normalizeRecord(SceneRecord record) {
    if (record.objectId.isZero() || !isKnownKind(record.kind) ||
        !record.worldBounds.isFiniteAndOrdered()) {
        return foundation::Result<SceneRecord>::failure(
            makeError(ErrorCode::kInvalidRecord, "SceneRecord is invalid"));
    }
    if (record.renderPayload.generation == 0 || record.hitGeometry.generation == 0) {
        return foundation::Result<SceneRecord>::failure(
            makeError(ErrorCode::kInvalidReference, "SceneRecord reference is invalid"));
    }
    record.worldBounds = foundation::canonicalizeRect(record.worldBounds);
    return foundation::Result<SceneRecord>::success(std::move(record));
}

const SceneRecord*
findRecord(std::span<const SceneRecord> records,
           const std::unordered_map<ObjectId, std::size_t, foundation::ObjectIdHash>& index,
           ObjectId objectId) {
    const auto found = index.find(objectId);
    return found == index.end() ? nullptr : &records[found->second];
}

WorldRect calculateContentBounds(std::span<const SceneRecord> records) {
    if (records.empty()) {
        return WorldRect{};
    }
    WorldRect bounds = records.front().worldBounds;
    for (const SceneRecord& record : records.subspan(1)) {
        bounds = foundation::unionRects(bounds, record.worldBounds);
    }
    return bounds;
}

} // namespace

foundation::Result<SceneRecordStore::PreparedUpdate>
SceneRecordStore::buildPreparedUpdate(std::vector<SceneRecord> records) {
    try {
        std::sort(records.begin(), records.end(), recordLess);
        std::unordered_map<ObjectId, std::size_t, foundation::ObjectIdHash> index;
        index.reserve(records.size());
        for (std::size_t position = 0; position < records.size(); ++position) {
            if (!index.emplace(records[position].objectId, position).second) {
                return foundation::Result<PreparedUpdate>::failure(
                    makeError(ErrorCode::kDuplicateObject, "Scene contains a duplicate ObjectId"));
            }
        }
        return foundation::Result<PreparedUpdate>::success(
            PreparedUpdate(std::move(records), std::move(index)));
    } catch (const std::bad_alloc&) {
        return foundation::Result<PreparedUpdate>::failure(
            makeError(ErrorCode::kOutOfMemory, "Unable to prepare SceneRecordStore"));
    }
}

const SceneRecord* SceneRecordStore::PreparedUpdate::find(ObjectId objectId) const {
    return findRecord(_records, _index, objectId);
}

WorldRect SceneRecordStore::PreparedUpdate::contentBounds() const {
    return calculateContentBounds(_records);
}

const SceneRecord* SceneRecordStore::find(ObjectId objectId) const {
    return findRecord(_records, _index, objectId);
}

WorldRect SceneRecordStore::contentBounds() const {
    return calculateContentBounds(_records);
}

std::size_t SceneRecordStore::estimatedBytes() const {
    return sizeof(*this) + _records.capacity() * sizeof(SceneRecord) +
           _index.size() * (sizeof(ObjectId) + sizeof(std::size_t) + 24U);
}

foundation::Result<SceneRecordStore::PreparedUpdate>
SceneRecordStore::prepareReplace(std::span<const SceneRecord> records) const {
    try {
        std::vector<SceneRecord> normalized;
        normalized.reserve(records.size());
        for (const SceneRecord& record : records) {
            auto result = normalizeRecord(record);
            if (!result) {
                return foundation::Result<PreparedUpdate>::failure(result.error());
            }
            normalized.push_back(std::move(result.value()));
        }
        return buildPreparedUpdate(std::move(normalized));
    } catch (const std::bad_alloc&) {
        return foundation::Result<PreparedUpdate>::failure(
            makeError(ErrorCode::kOutOfMemory, "Unable to prepare Scene replacement"));
    }
}

foundation::Result<SceneRecordStore::PreparedUpdate>
SceneRecordStore::prepareApply(std::span<const SceneMutation> mutations) const {
    try {
        std::vector<SceneRecord> records = _records;
        std::unordered_set<ObjectId, foundation::ObjectIdHash> mutatedIds;
        mutatedIds.reserve(mutations.size());
        for (const SceneMutation& mutation : mutations) {
            if (mutation.objectId.isZero() || !mutatedIds.insert(mutation.objectId).second) {
                return foundation::Result<PreparedUpdate>::failure(makeError(
                    ErrorCode::kDuplicateObject, "Delta mutates an ObjectId more than once"));
            }

            auto found = std::find_if(
                records.begin(), records.end(), [&mutation](const SceneRecord& record) {
                    return record.objectId == mutation.objectId;
                });
            const bool exists = found != records.end();
            const bool hasBefore = mutation.before.has_value();
            const bool hasAfter = mutation.after.has_value();
            if ((hasBefore && mutation.before->objectId != mutation.objectId) ||
                (hasAfter && mutation.after->objectId != mutation.objectId)) {
                return foundation::Result<PreparedUpdate>::failure(makeError(
                    ErrorCode::kInvalidRecord, "Mutation ObjectId does not match its record"));
            }

            std::optional<SceneRecord> normalizedAfter;
            if (hasAfter) {
                auto result = normalizeRecord(*mutation.after);
                if (!result) {
                    return foundation::Result<PreparedUpdate>::failure(result.error());
                }
                normalizedAfter = std::move(result.value());
            }

            switch (mutation.kind) {
            case SceneMutationKind::kInsert:
                if (hasBefore || !normalizedAfter) {
                    return foundation::Result<PreparedUpdate>::failure(makeError(
                        ErrorCode::kInvalidRecord, "Insert requires only an after record"));
                }
                if (exists) {
                    return foundation::Result<PreparedUpdate>::failure(
                        makeError(ErrorCode::kDuplicateObject, "Insert ObjectId already exists"));
                }
                records.push_back(std::move(*normalizedAfter));
                break;
            case SceneMutationKind::kUpdate:
                if (!hasBefore || !normalizedAfter) {
                    return foundation::Result<PreparedUpdate>::failure(makeError(
                        ErrorCode::kInvalidRecord, "Update requires before and after records"));
                }
                if (!exists) {
                    return foundation::Result<PreparedUpdate>::failure(
                        makeError(ErrorCode::kMissingObject, "Update ObjectId does not exist"));
                }
                if (*found != *mutation.before) {
                    return foundation::Result<PreparedUpdate>::failure(
                        makeError(ErrorCode::kBeforeImageMismatch,
                                  "Update before image does not match Scene"));
                }
                *found = std::move(*normalizedAfter);
                break;
            case SceneMutationKind::kRemove:
                if (!hasBefore || hasAfter) {
                    return foundation::Result<PreparedUpdate>::failure(makeError(
                        ErrorCode::kInvalidRecord, "Remove requires only a before record"));
                }
                if (!exists) {
                    return foundation::Result<PreparedUpdate>::failure(
                        makeError(ErrorCode::kMissingObject, "Remove ObjectId does not exist"));
                }
                if (*found != *mutation.before) {
                    return foundation::Result<PreparedUpdate>::failure(
                        makeError(ErrorCode::kBeforeImageMismatch,
                                  "Remove before image does not match Scene"));
                }
                records.erase(found);
                break;
            default:
                return foundation::Result<PreparedUpdate>::failure(
                    makeError(ErrorCode::kInvalidRecord, "Mutation kind is unknown"));
            }
        }
        return buildPreparedUpdate(std::move(records));
    } catch (const std::bad_alloc&) {
        return foundation::Result<PreparedUpdate>::failure(
            makeError(ErrorCode::kOutOfMemory, "Unable to prepare Scene delta"));
    }
}

void SceneRecordStore::commit(PreparedUpdate update) noexcept {
    _records.swap(update._records);
    _index.swap(update._index);
}

} // namespace canvas
