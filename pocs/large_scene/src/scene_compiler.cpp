#include "canvas/poc03/large_scene.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include "internal.h"

namespace canvas::poc03 {
namespace {

Bounds DirtyFor(const SemanticChange& change) {
  if (change.before && change.after) {
    return Bounds::Union(change.before->bounds, change.after->bounds);
  }
  return change.before ? change.before->bounds : change.after->bounds;
}

bool ChangeMatchesDocument(const Document& document,
                           const SemanticChange& change) {
  const NodeRecord* authoritative = document.Find(change.id);
  if (change.kind == OperationKind::kCreate) {
    return !change.before.has_value() && change.after.has_value() &&
           authoritative != nullptr && *authoritative == *change.after;
  }
  if (change.kind == OperationKind::kDelete) {
    return change.before.has_value() && !change.after.has_value() &&
           authoritative == nullptr;
  }
  return change.before.has_value() && change.after.has_value() &&
         authoritative != nullptr && *authoritative == *change.after;
}

}  // namespace

std::optional<NodeRecord> RuntimeScene::RecordAt(uint32_t slot) const {
  if (slot >= active_.size() || active_[slot] == 0U) {
    return std::nullopt;
  }
  return NodeRecord{ids_[slot], orders_[slot], types_[slot], bounds_[slot],
                    colors_[slot], resource_keys_[slot],
                    content_revisions_[slot], locked_[slot] != 0U};
}

std::optional<uint32_t> RuntimeScene::SlotFor(uint64_t id) const {
  const auto found = id_to_slot_.find(id);
  return found == id_to_slot_.end() ? std::nullopt
                                    : std::optional<uint32_t>(found->second);
}

std::string RuntimeScene::Digest() const {
  std::vector<uint8_t> bytes;
  bytes.reserve(active_count() * 56U + 24U);
  detail::EncodeU32(&bytes, 1U);
  detail::EncodeU64(&bytes, source_revision_);
  detail::EncodeU64(&bytes, static_cast<uint64_t>(active_count()));
  for (const uint32_t slot : draw_order_) {
    const auto record = RecordAt(slot);
    if (!record) {
      throw std::logic_error("draw order contains an inactive record");
    }
    detail::EncodeRecord(&bytes, *record);
  }
  return CanonicalDigest(bytes);
}

Bounds RuntimeScene::ContentBounds() const {
  bool initialized = false;
  Bounds result;
  for (const uint32_t slot : draw_order_) {
    if (!initialized) {
      result = bounds_[slot];
      initialized = true;
    } else {
      result = Bounds::Union(result, bounds_[slot]);
    }
  }
  return result;
}

size_t RuntimeScene::EstimatedBytes() const {
  return sizeof(*this) + ids_.capacity() * sizeof(uint64_t) +
         orders_.capacity() * sizeof(uint32_t) +
         types_.capacity() * sizeof(NodeType) +
         bounds_.capacity() * sizeof(Bounds) +
         colors_.capacity() * sizeof(uint32_t) +
         resource_keys_.capacity() * sizeof(uint64_t) +
         content_revisions_.capacity() * sizeof(uint64_t) +
         locked_.capacity() + active_.capacity() +
         free_slots_.capacity() * sizeof(uint32_t) +
         draw_order_.capacity() * sizeof(uint32_t) +
         id_to_slot_.size() * 40U + spatial_index_.EstimatedBytes();
}

RuntimeScene SceneCompiler::CompileFull(
    const Document& document, CompileDiagnostics* diagnostics) const {
  RuntimeScene scene;
  const std::vector<const NodeRecord*> records = document.OrderedRecords();
  if (records.size() >= std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("POC-03 scene exceeds uint32 slot capacity");
  }
  scene.ids_.reserve(records.size());
  scene.orders_.reserve(records.size());
  scene.types_.reserve(records.size());
  scene.bounds_.reserve(records.size());
  scene.colors_.reserve(records.size());
  scene.resource_keys_.reserve(records.size());
  scene.content_revisions_.reserve(records.size());
  scene.locked_.reserve(records.size());
  scene.active_.reserve(records.size());
  scene.draw_order_.reserve(records.size());
  scene.id_to_slot_.reserve(records.size());
  scene.spatial_index_.Clear(records.size());
  for (const NodeRecord* record : records) {
    const uint32_t slot = static_cast<uint32_t>(scene.ids_.size());
    scene.ids_.push_back(record->id);
    scene.orders_.push_back(record->order);
    scene.types_.push_back(record->type);
    scene.bounds_.push_back(record->bounds);
    scene.colors_.push_back(record->rgba);
    scene.resource_keys_.push_back(record->resource_key);
    scene.content_revisions_.push_back(record->content_revision);
    scene.locked_.push_back(record->locked ? 1U : 0U);
    scene.active_.push_back(1U);
    scene.id_to_slot_.emplace(record->id, slot);
    scene.draw_order_.push_back(slot);
    scene.spatial_index_.Insert(slot, record->bounds);
  }
  scene.source_revision_ = document.revision();
  if (diagnostics != nullptr) {
    diagnostics->records_touched += records.size();
    diagnostics->spatial_records_touched += records.size();
    diagnostics->order_records_visited += records.size();
    if (!records.empty()) {
      diagnostics->authoritative_world_dirty = scene.ContentBounds();
    }
  }
  return scene;
}

bool SceneCompiler::ApplyIncremental(const Document& document,
                                     const ChangeSet& changes,
                                     RuntimeScene* scene,
                                     CompileDiagnostics* diagnostics,
                                     std::string* error) const {
  if (scene == nullptr || diagnostics == nullptr) {
    if (error != nullptr) {
      *error = "scene and diagnostics must not be null";
    }
    return false;
  }
  const bool revisions_match =
      changes.before_revision == scene->source_revision_ &&
      changes.after_revision == document.revision() &&
      changes.after_revision == changes.before_revision + 1U;
  const bool semantic_valid = !changes.semantic_changes.empty() &&
      std::all_of(changes.semantic_changes.begin(),
                  changes.semantic_changes.end(),
                  [&](const SemanticChange& change) {
                    return ChangeMatchesDocument(document, change);
                  });
  if (!revisions_match || !semantic_valid) {
    ++diagnostics->full_fallbacks;
    *scene = CompileFull(document, diagnostics);
    if (error != nullptr) {
      *error = "semantic/revision mismatch forced a safe full compile";
    }
    return true;
  }

  bool dirty_initialized = false;
  Bounds authoritative_dirty;
  for (const SemanticChange& change : changes.semantic_changes) {
    const Bounds change_dirty = DirtyFor(change);
    authoritative_dirty = dirty_initialized
                              ? Bounds::Union(authoritative_dirty, change_dirty)
                              : change_dirty;
    dirty_initialized = true;
    const auto existing = scene->SlotFor(change.id);
    if (change.kind == OperationKind::kCreate) {
      if (existing || !change.after) {
        ++diagnostics->full_fallbacks;
        *scene = CompileFull(document, diagnostics);
        return true;
      }
      uint32_t slot = 0;
      if (scene->free_slots_.empty()) {
        slot = static_cast<uint32_t>(scene->ids_.size());
        scene->ids_.push_back(change.after->id);
        scene->orders_.push_back(change.after->order);
        scene->types_.push_back(change.after->type);
        scene->bounds_.push_back(change.after->bounds);
        scene->colors_.push_back(change.after->rgba);
        scene->resource_keys_.push_back(change.after->resource_key);
        scene->content_revisions_.push_back(change.after->content_revision);
        scene->locked_.push_back(change.after->locked ? 1U : 0U);
        scene->active_.push_back(1U);
      } else {
        slot = scene->free_slots_.back();
        scene->free_slots_.pop_back();
        scene->ids_[slot] = change.after->id;
        scene->orders_[slot] = change.after->order;
        scene->types_[slot] = change.after->type;
        scene->bounds_[slot] = change.after->bounds;
        scene->colors_[slot] = change.after->rgba;
        scene->resource_keys_[slot] = change.after->resource_key;
        scene->content_revisions_[slot] = change.after->content_revision;
        scene->locked_[slot] = change.after->locked ? 1U : 0U;
        scene->active_[slot] = 1U;
      }
      scene->id_to_slot_[change.id] = slot;
      const auto position = std::lower_bound(
          scene->draw_order_.begin(), scene->draw_order_.end(), slot,
          [&](uint32_t left, uint32_t right) {
            ++diagnostics->order_records_visited;
            return scene->orders_[left] < scene->orders_[right] ||
                   (scene->orders_[left] == scene->orders_[right] &&
                    scene->ids_[left] < scene->ids_[right]);
          });
      scene->draw_order_.insert(position, slot);
      scene->spatial_index_.Insert(slot, change.after->bounds);
    } else if (change.kind == OperationKind::kDelete) {
      const auto scene_before = existing ? scene->RecordAt(*existing)
                                         : std::nullopt;
      if (!existing || !change.before || !scene_before ||
          *scene_before != *change.before) {
        ++diagnostics->full_fallbacks;
        *scene = CompileFull(document, diagnostics);
        return true;
      }
      const uint32_t slot = *existing;
      scene->spatial_index_.Remove(slot, scene->bounds_[slot]);
      scene->id_to_slot_.erase(change.id);
      scene->active_[slot] = 0U;
      scene->free_slots_.push_back(slot);
      const auto position = std::find(scene->draw_order_.begin(),
                                      scene->draw_order_.end(), slot);
      diagnostics->order_records_visited += static_cast<size_t>(
          std::distance(scene->draw_order_.begin(), position)) + 1U;
      if (position != scene->draw_order_.end()) {
        scene->draw_order_.erase(position);
      }
    } else {
      const auto scene_before = existing ? scene->RecordAt(*existing)
                                         : std::nullopt;
      if (!existing || !change.before || !change.after || !scene_before ||
          *scene_before != *change.before) {
        ++diagnostics->full_fallbacks;
        *scene = CompileFull(document, diagnostics);
        return true;
      }
      const uint32_t slot = *existing;
      scene->spatial_index_.Update(slot, scene->bounds_[slot],
                                   change.after->bounds);
      scene->orders_[slot] = change.after->order;
      scene->types_[slot] = change.after->type;
      scene->bounds_[slot] = change.after->bounds;
      scene->colors_[slot] = change.after->rgba;
      scene->resource_keys_[slot] = change.after->resource_key;
      scene->content_revisions_[slot] = change.after->content_revision;
      scene->locked_[slot] = change.after->locked ? 1U : 0U;
      if (change.kind == OperationKind::kReorder) {
        std::sort(scene->draw_order_.begin(), scene->draw_order_.end(),
                  [&](uint32_t left, uint32_t right) {
                    ++diagnostics->order_records_visited;
                    return scene->orders_[left] < scene->orders_[right] ||
                           (scene->orders_[left] == scene->orders_[right] &&
                            scene->ids_[left] < scene->ids_[right]);
                  });
      }
    }
    ++diagnostics->records_touched;
    ++diagnostics->spatial_records_touched;
  }
  scene->source_revision_ = changes.after_revision;
  diagnostics->authoritative_world_dirty = authoritative_dirty;

  if (changes.hints) {
    const bool hint_valid =
        changes.hints->before_revision == changes.before_revision &&
        changes.hints->after_revision == changes.after_revision &&
        changes.hints->world_dirty.has_value() &&
        changes.hints->world_dirty->IsFiniteAndOrdered() &&
        changes.hints->world_dirty->Contains(authoritative_dirty);
    if (!hint_valid) {
      ++diagnostics->rejected_hints;
    }
  }
  return true;
}

}  // namespace canvas::poc03
