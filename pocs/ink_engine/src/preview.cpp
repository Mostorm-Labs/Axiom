#include "canvas_poc02/ink_engine.h"

#include <algorithm>

#include "foundation.h"

namespace canvas::poc02 {

Status DefaultPreviewSink::Begin(StrokeId id, const BrushDescriptor& brush) {
  if (id == 0 || internal::ValidateBrush(brush) != Status::kOk) {
    return Status::kInvalidArgument;
  }
  if (states_.contains(id)) return Status::kInvalidState;
  State state;
  state.brush = brush;
  states_.emplace(id, std::move(state));
  events_.push_back(PreviewEvent{.type = PreviewEventType::kBegin,
                                 .stroke_id = id});
  return Status::kOk;
}

Status DefaultPreviewSink::Push(const PreviewStrokeUpdate& update) {
  if (update.schema_version != PreviewStrokeUpdate::kSchemaVersion) {
    return Status::kUnsupportedVersion;
  }
  auto iterator = states_.find(update.stroke_id);
  if (iterator == states_.end()) return Status::kNotFound;
  State& state = iterator->second;
  if (state.committed || update.revision == 0 || update.revision <= state.revision ||
      update.brush != state.brush ||
      update.truncate_confirmed_to > state.confirmed.size()) {
    return Status::kInvalidState;
  }
  state.confirmed.resize(update.truncate_confirmed_to);
  state.confirmed.insert(state.confirmed.end(), update.confirmed_append.begin(),
                         update.confirmed_append.end());
  state.predicted = update.predicted_tail;
  state.revision = update.revision;
  events_.push_back(PreviewEvent{.type = PreviewEventType::kUpdate,
                                 .stroke_id = update.stroke_id,
                                 .revision = update.revision});
  return Status::kOk;
}

Status DefaultPreviewSink::CanonicalCommitted(StrokeId id,
                                               uint64_t document_revision) {
  auto iterator = states_.find(id);
  if (iterator == states_.end()) return Status::kNotFound;
  if (iterator->second.committed) return Status::kInvalidState;
  iterator->second.committed = true;
  iterator->second.predicted.clear();
  events_.push_back(PreviewEvent{.type = PreviewEventType::kCanonicalCommitted,
                                 .stroke_id = id,
                                 .revision = iterator->second.revision,
                                 .document_revision = document_revision});
  return Status::kOk;
}

Status DefaultPreviewSink::CanonicalVisible(StrokeId id,
                                             uint64_t document_revision) {
  auto iterator = states_.find(id);
  if (iterator == states_.end()) return Status::kNotFound;
  if (!iterator->second.committed || iterator->second.visible) {
    return Status::kInvalidState;
  }
  iterator->second.visible = true;
  events_.push_back(PreviewEvent{.type = PreviewEventType::kCanonicalVisible,
                                 .stroke_id = id,
                                 .revision = iterator->second.revision,
                                 .document_revision = document_revision});
  return Status::kOk;
}

Status DefaultPreviewSink::Cancel(StrokeId id) {
  auto iterator = states_.find(id);
  if (iterator == states_.end()) return Status::kNotFound;
  events_.push_back(PreviewEvent{.type = PreviewEventType::kCancel,
                                 .stroke_id = id,
                                 .revision = iterator->second.revision});
  states_.erase(iterator);
  return Status::kOk;
}

const DefaultPreviewSink::State* DefaultPreviewSink::Find(StrokeId id) const {
  const auto iterator = states_.find(id);
  return iterator == states_.end() ? nullptr : &iterator->second;
}

std::string DefaultPreviewSink::ModelDigest() const {
  internal::CanonicalEncoder encoder;
  encoder.String("canvas-poc02-preview-model-v1");
  encoder.U64(states_.size());
  for (const auto& [id, state] : states_) {
    encoder.U64(id);
    internal::EncodeBrush(state.brush, &encoder);
    encoder.U64(state.revision);
    encoder.U8(state.committed ? 1U : 0U);
    encoder.U8(state.visible ? 1U : 0U);
    const auto encode_primitives = [&encoder](const auto& primitives) {
      encoder.U64(primitives.size());
      for (const auto& primitive : primitives) {
        encoder.F32(primitive.position.x);
        encoder.F32(primitive.position.y);
        encoder.F32(primitive.radius);
        encoder.F32(primitive.rotation_degrees);
        encoder.F32(primitive.opacity);
      }
    };
    encode_primitives(state.confirmed);
    encode_primitives(state.predicted);
  }
  encoder.U64(events_.size());
  for (const auto& event : events_) {
    encoder.U8(static_cast<uint8_t>(event.type));
    encoder.U64(event.stroke_id);
    encoder.U64(event.revision);
    encoder.U64(event.document_revision);
  }
  return internal::HashHex(encoder.bytes());
}

namespace {

size_t PreviewBytes(const PreviewStrokeUpdate& update) {
  return sizeof(PreviewStrokeUpdate) +
      (update.confirmed_append.size() + update.predicted_tail.size()) *
          sizeof(PreviewPrimitive) + update.brush.resource_id.size() +
      update.brush.resource_content_hash.size();
}

bool CanReplace(const PreviewStrokeUpdate& prior,
                const PreviewStrokeUpdate& newer) {
  return prior.schema_version == newer.schema_version &&
         prior.stroke_id == newer.stroke_id && prior.view_id == newer.view_id &&
         prior.viewport_revision == newer.viewport_revision &&
         prior.brush == newer.brush && newer.revision > prior.revision &&
         newer.truncate_confirmed_to >= prior.truncate_confirmed_to &&
         newer.truncate_confirmed_to <=
             prior.truncate_confirmed_to + prior.confirmed_append.size();
}

PreviewStrokeUpdate MergePreview(const PreviewStrokeUpdate& prior,
                                 PreviewStrokeUpdate newer) {
  const size_t retained = newer.truncate_confirmed_to -
                          prior.truncate_confirmed_to;
  std::vector<PreviewPrimitive> merged;
  merged.reserve(retained + newer.confirmed_append.size());
  merged.insert(merged.end(), prior.confirmed_append.begin(),
                prior.confirmed_append.begin() +
                    static_cast<std::ptrdiff_t>(retained));
  merged.insert(merged.end(), newer.confirmed_append.begin(),
                newer.confirmed_append.end());
  newer.truncate_confirmed_to = prior.truncate_confirmed_to;
  newer.confirmed_append = std::move(merged);
  return newer;
}

}  // namespace

PreviewUpdateQueue::PreviewUpdateQueue(PreviewQueueLimits limits)
    : limits_(limits) {}

Status PreviewUpdateQueue::Enqueue(PreviewStrokeUpdate update) {
  if (update.schema_version != PreviewStrokeUpdate::kSchemaVersion ||
      update.stroke_id == 0 || update.revision == 0) {
    return Status::kInvalidArgument;
  }
  bool coalesced = false;
  size_t removed_primitives = 0;
  size_t removed_bytes = 0;
  if (!updates_.empty() && CanReplace(updates_.back(), update)) {
    removed_primitives = updates_.back().confirmed_append.size() +
                         updates_.back().predicted_tail.size();
    removed_bytes = PreviewBytes(updates_.back());
    update = MergePreview(updates_.back(), std::move(update));
    coalesced = true;
  }
  const size_t primitives = update.confirmed_append.size() +
                            update.predicted_tail.size();
  const size_t projected_updates = updates_.size() + (coalesced ? 0U : 1U);
  const size_t projected_primitives = diagnostics_.primitives - removed_primitives +
                                      primitives;
  const size_t projected_bytes = diagnostics_.bytes - removed_bytes +
                                 PreviewBytes(update);
  if (projected_updates > limits_.max_updates ||
      projected_primitives > limits_.max_primitives ||
      projected_bytes > limits_.max_bytes) {
    ++diagnostics_.overruns;
    return Status::kInputOverrun;
  }
  if (coalesced) {
    updates_.back() = std::move(update);
    ++diagnostics_.coalesced_updates;
  } else {
    updates_.push_back(std::move(update));
  }
  RefreshDiagnostics();
  return Status::kOk;
}

std::optional<PreviewStrokeUpdate> PreviewUpdateQueue::Pop() {
  if (updates_.empty()) return std::nullopt;
  PreviewStrokeUpdate update = std::move(updates_.front());
  updates_.pop_front();
  RefreshDiagnostics();
  return update;
}

void PreviewUpdateQueue::Clear() {
  updates_.clear();
  RefreshDiagnostics();
}

void PreviewUpdateQueue::RefreshDiagnostics() {
  diagnostics_.updates = updates_.size();
  diagnostics_.primitives = 0;
  diagnostics_.bytes = 0;
  for (const auto& update : updates_) {
    diagnostics_.primitives += update.confirmed_append.size() +
                               update.predicted_tail.size();
    diagnostics_.bytes += PreviewBytes(update);
  }
}

}  // namespace canvas::poc02
