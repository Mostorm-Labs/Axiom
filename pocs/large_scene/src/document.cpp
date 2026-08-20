#include "canvas/poc03/large_scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "internal.h"

namespace canvas::poc03 {

const NodeRecord* Document::Find(uint64_t id) const {
  const auto found = id_to_slot_.find(id);
  if (found == id_to_slot_.end()) {
    return nullptr;
  }
  return &slots_[found->second].node;
}

std::vector<const NodeRecord*> Document::OrderedRecords() const {
  std::vector<const NodeRecord*> records;
  records.reserve(id_to_slot_.size());
  for (const auto& [id, slot] : id_to_slot_) {
    static_cast<void>(id);
    records.push_back(&slots_[slot].node);
  }
  std::sort(records.begin(), records.end(), [](const NodeRecord* left,
                                                const NodeRecord* right) {
    return left->order < right->order ||
           (left->order == right->order && left->id < right->id);
  });
  return records;
}

std::string Document::Digest() const {
  std::vector<uint8_t> bytes;
  bytes.reserve(active_count() * 56U + 24U);
  detail::EncodeU32(&bytes, 1U);
  detail::EncodeU64(&bytes, revision_);
  detail::EncodeU64(&bytes, static_cast<uint64_t>(active_count()));
  for (const NodeRecord* node : OrderedRecords()) {
    detail::EncodeRecord(&bytes, *node);
  }
  return CanonicalDigest(bytes);
}

size_t Document::EstimatedBytes() const {
  return sizeof(*this) + slots_.capacity() * sizeof(Slot) +
         free_slots_.capacity() * sizeof(uint32_t) +
         id_to_slot_.size() * (sizeof(uint64_t) + sizeof(uint32_t) + 24U);
}

bool Document::Apply(const Operation& operation, ChangeSet* change_set,
                     std::string* error) {
  if (change_set == nullptr) {
    if (error != nullptr) {
      *error = "change_set must not be null";
    }
    return false;
  }
  ChangeSet next;
  next.before_revision = revision_;
  const NodeRecord* before = Find(operation.id);
  const uint8_t kind = static_cast<uint8_t>(operation.kind);
  if (kind > static_cast<uint8_t>(OperationKind::kReorder)) {
    if (error != nullptr) {
      *error = "operation kind is unknown";
    }
    return false;
  }

  if (operation.kind == OperationKind::kCreate) {
    if (!operation.value || operation.value->id != operation.id ||
        before != nullptr || !detail::ValidRecord(*operation.value, error)) {
      if (error != nullptr && error->empty()) {
        *error = "create requires a valid unique node with matching id";
      }
      return false;
    }
  } else if (operation.kind == OperationKind::kDelete) {
    if (before == nullptr || operation.value.has_value()) {
      if (error != nullptr) {
        *error = "delete requires an existing node and no value";
      }
      return false;
    }
  } else {
    if (before == nullptr || !operation.value ||
        operation.value->id != operation.id ||
        !detail::ValidRecord(*operation.value, error)) {
      if (error != nullptr && error->empty()) {
        *error = "update/reorder requires an existing valid node";
      }
      return false;
    }
    if (operation.kind == OperationKind::kUpdate &&
        operation.value->order != before->order) {
      if (error != nullptr) {
        *error = "update cannot change order; use reorder";
      }
      return false;
    }
    if (operation.kind == OperationKind::kReorder) {
      NodeRecord expected = *before;
      expected.order = operation.value->order;
      if (expected != *operation.value) {
        if (error != nullptr) {
          *error = "reorder may only change the order field";
        }
        return false;
      }
    }
  }

  if (revision_ == std::numeric_limits<uint64_t>::max()) {
    if (error != nullptr) {
      *error = "document revision overflow";
    }
    return false;
  }

  SemanticChange semantic;
  semantic.kind = operation.kind;
  semantic.id = operation.id;
  if (before != nullptr) {
    semantic.before = *before;
  }

  if (operation.kind == OperationKind::kCreate) {
    uint32_t slot = 0;
    if (free_slots_.empty()) {
      if (slots_.size() >= std::numeric_limits<uint32_t>::max()) {
        if (error != nullptr) {
          *error = "document slot capacity overflow";
        }
        return false;
      }
      slot = static_cast<uint32_t>(slots_.size());
      slots_.push_back(Slot{*operation.value, true});
    } else {
      slot = free_slots_.back();
      free_slots_.pop_back();
      slots_[slot] = Slot{*operation.value, true};
    }
    id_to_slot_.emplace(operation.id, slot);
    semantic.after = *operation.value;
  } else if (operation.kind == OperationKind::kDelete) {
    const uint32_t slot = id_to_slot_.at(operation.id);
    slots_[slot].active = false;
    id_to_slot_.erase(operation.id);
    free_slots_.push_back(slot);
  } else {
    const uint32_t slot = id_to_slot_.at(operation.id);
    slots_[slot].node = *operation.value;
    semantic.after = *operation.value;
  }

  ++revision_;
  next.after_revision = revision_;
  next.semantic_changes.push_back(semantic);
  InvalidationHints hints;
  hints.before_revision = next.before_revision;
  hints.after_revision = next.after_revision;
  if (semantic.before && semantic.after) {
    hints.world_dirty = Bounds::Union(semantic.before->bounds,
                                      semantic.after->bounds);
  } else if (semantic.before) {
    hints.world_dirty = semantic.before->bounds;
  } else if (semantic.after) {
    hints.world_dirty = semantic.after->bounds;
  }
  next.hints = hints;
  *change_set = std::move(next);
  return true;
}

namespace {

class DeterministicRandom {
 public:
  explicit DeterministicRandom(uint64_t seed) : state_(seed) {}
  uint32_t Next() {
    state_ += 0x9e3779b97f4a7c15ULL;
    uint64_t value = state_;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return static_cast<uint32_t>((value ^ (value >> 31U)) >> 32U);
  }

 private:
  uint64_t state_;
};

}  // namespace

Document GenerateDocument(const GeneratorConfig& config) {
  if (config.node_count == 0 || config.columns == 0 ||
      config.cell_size < 8.0F || !std::isfinite(config.cell_size)) {
    throw std::invalid_argument("invalid deterministic generator config");
  }
  Document document;
  DeterministicRandom random(config.seed);
  std::string error;
  for (uint32_t index = 0; index < config.node_count; ++index) {
    const uint32_t column = index % config.columns;
    const uint32_t row = index / config.columns;
    const uint32_t jitter_x = random.Next() & 7U;
    const uint32_t jitter_y = random.Next() & 7U;
    const float left = static_cast<float>(column) * config.cell_size +
                       static_cast<float>(jitter_x);
    const float top = static_cast<float>(row) * config.cell_size +
                      static_cast<float>(jitter_y);
    const float width = 12.0F + static_cast<float>(random.Next() & 15U);
    const float height = 12.0F + static_cast<float>(random.Next() & 15U);
    NodeRecord node;
    node.id = static_cast<uint64_t>(index) + 1U;
    node.order = index;
    node.type = static_cast<NodeType>((index % 5U) + 1U);
    node.bounds = Bounds{left, top, left + width, top + height};
    node.rgba = 0xff000000U | (random.Next() & 0x00ffffffU);
    node.resource_key = node.type == NodeType::kImage ||
                                node.type == NodeType::kSimpleText
                            ? 1U + (index % 17U)
                            : 0U;
    ChangeSet ignored;
    if (!document.Apply(Operation{OperationKind::kCreate, node.id, node},
                        &ignored, &error)) {
      throw std::runtime_error("deterministic generator failed: " + error);
    }
  }
  return document;
}

}  // namespace canvas::poc03
