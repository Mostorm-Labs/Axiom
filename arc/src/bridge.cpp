#include "arc/arc.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <utility>

namespace arc {
namespace {

template <typename T>
bool ValidHeader(const T& value) {
  return value.struct_size >= sizeof(T) && value.abi_version == ARC_ABI_VERSION;
}

bool SameToken(const arc_handoff_token_v0& left,
               const arc_handoff_token_v0& right) {
  return left.high == right.high && left.low == right.low;
}

bool NonZeroToken(const arc_handoff_token_v0& token) {
  return token.high != 0 || token.low != 0;
}

bool ValidStride(uint32_t count, uint32_t stride, size_t minimum,
                 const void* data) {
  if (count == 0) return data == nullptr || stride >= minimum;
  return data != nullptr && stride >= minimum;
}

const arc_preview_primitive_v0* PrimitiveAt(
    const arc_preview_primitive_v0* base, uint32_t stride, uint32_t index) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(base);
  return reinterpret_cast<const arc_preview_primitive_v0*>(
      bytes + static_cast<size_t>(stride) * index);
}

struct OwnedBegin {
  arc_preview_begin_v0 value{};
  std::string resource_id;
  std::string resource_hash;
};

struct StrokeState {
  StrokeSnapshot snapshot;
  OwnedBegin begin;
};

bool SameFloat(float left, float right) {
  return std::bit_cast<uint32_t>(left) == std::bit_cast<uint32_t>(right);
}

bool SameTransform(const arc_affine_transform_v0& left,
                   const arc_affine_transform_v0& right) {
  return SameFloat(left.m00, right.m00) && SameFloat(left.m01, right.m01) &&
         SameFloat(left.m10, right.m10) && SameFloat(left.m11, right.m11) &&
         SameFloat(left.tx, right.tx) && SameFloat(left.ty, right.ty);
}

bool SameBegin(const StrokeState& owned, const arc_preview_begin_v0& incoming) {
  const auto& left = owned.begin.value;
  const auto& left_brush = left.brush;
  const auto& right_brush = incoming.brush;
  const std::string_view right_resource{
      right_brush.resource_id == nullptr ? "" : right_brush.resource_id,
      right_brush.resource_id_size};
  const std::string_view right_hash{
      right_brush.resource_content_hash == nullptr
          ? ""
          : right_brush.resource_content_hash,
      right_brush.resource_content_hash_size};
  return left.schema_version == incoming.schema_version &&
         left.coordinate_space == incoming.coordinate_space &&
         left.stroke_id == incoming.stroke_id && left.view_id == incoming.view_id &&
         left.viewport_revision == incoming.viewport_revision &&
         left.target_generation == incoming.target_generation &&
         SameTransform(left.source_to_device, incoming.source_to_device) &&
         left_brush.schema_version == right_brush.schema_version &&
         left_brush.brush_type == right_brush.brush_type &&
         left_brush.brush_version == right_brush.brush_version &&
         left_brush.algorithm_version == right_brush.algorithm_version &&
         std::memcmp(left_brush.color_rgba, right_brush.color_rgba,
                     sizeof(left_brush.color_rgba)) == 0 &&
         SameFloat(left_brush.size, right_brush.size) &&
         SameFloat(left_brush.spacing, right_brush.spacing) &&
         SameFloat(left_brush.opacity, right_brush.opacity) &&
         SameFloat(left_brush.jitter, right_brush.jitter) &&
         owned.begin.resource_id == right_resource &&
         owned.begin.resource_hash == right_hash;
}

class NullBackend final : public PreviewBackend {
 public:
  arc_backend_capabilities_v0 Capabilities() const override {
    return {.struct_size = sizeof(arc_backend_capabilities_v0),
            .abi_version = ARC_ABI_VERSION,
            .platform_kind = ARC_PLATFORM_HEADLESS,
            .presentation_capabilities =
                ARC_PRESENTATION_CAPABILITY_INDEPENDENT_TARGET |
                ARC_PRESENTATION_CAPABILITY_REPLACE_TRUNCATE |
                ARC_PRESENTATION_CAPABILITY_PRESENT_RECEIPT,
            .max_pending_strokes = std::numeric_limits<uint32_t>::max(),
            .max_primitives_per_update = std::numeric_limits<uint32_t>::max(),
            .max_queue_bytes = std::numeric_limits<uint64_t>::max()};
  }
  Status Attach(const arc_preview_target_v0&) override { return Status::kOk; }
  Status Detach(uint64_t) override { return Status::kOk; }
  Status Begin(const arc_preview_begin_v0&) override { return Status::kOk; }
  Status Push(const arc_preview_update_v0&) override { return Status::kOk; }
  Status SealInput(const arc_preview_seal_v0&) override { return Status::kOk; }
  Status CanonicalCommitted(const arc_canonical_commit_v0&) override {
    return Status::kOk;
  }
  Status CanonicalVisible(const arc_canonical_visible_v0&) override {
    return Status::kOk;
  }
  Status Cancel(const arc_preview_cancel_v0&) override { return Status::kOk; }
};

}  // namespace

std::string_view StatusName(Status status) {
  switch (status) {
    case Status::kOk:
      return "ok";
    case Status::kInvalidArgument:
      return "invalid_argument";
    case Status::kAbiMismatch:
      return "abi_mismatch";
    case Status::kInvalidState:
      return "invalid_state";
    case Status::kStaleRevision:
      return "stale_revision";
    case Status::kNotFound:
      return "not_found";
    case Status::kCapacityExceeded:
      return "capacity_exceeded";
    case Status::kBackendUnavailable:
      return "backend_unavailable";
    case Status::kPresentationFailed:
      return "presentation_failed";
    case Status::kSurfaceLost:
      return "surface_lost";
    case Status::kInternalError:
      return "internal_error";
  }
  return "unknown";
}

class Bridge::Impl {
 public:
  Impl(std::unique_ptr<PreviewBackend> primary,
       std::unique_ptr<PreviewBackend> fallback, BridgeLimits limits)
      : primary_(std::move(primary)), fallback_(std::move(fallback)), limits_(limits) {
    if (!fallback_) fallback_ = CreateNullBackend();
  }

  PreviewBackend* backend() const {
    return use_fallback_ || !primary_ ? fallback_.get() : primary_.get();
  }

  Status Invoke(Status status, std::string_view operation) {
    if (status == Status::kOk) return Status::kOk;
    ++diagnostics_.backend_failures;
    diagnostics_.last_backend_error = status;
    diagnostics_.last_backend_message = std::string(operation);
    canonical_redraw_requested_ = true;
    if (!use_fallback_) {
      use_fallback_ = true;
      ++diagnostics_.fallback_activations;
      ReplayFallback();
    }
    return Status::kOk;
  }

  void DegradeForCapacity(std::string_view operation) {
    ++diagnostics_.capacity_degradations;
    (void)Invoke(Status::kCapacityExceeded, operation);
  }

  void DropPreviewGeometry() {
    for (auto& [id, state] : strokes_) {
      (void)id;
      state.snapshot.confirmed.clear();
      state.snapshot.predicted.clear();
      state.snapshot.preview_suppressed = true;
    }
  }

  void RememberRetired(uint64_t stroke_id,
                       const arc_handoff_token_v0& token) {
    if (limits_.max_retired_tombstones == 0) return;
    retired_[stroke_id] = token;
    retired_order_.push_back({stroke_id, token});
    while (retired_order_.size() > limits_.max_retired_tombstones) {
      const auto [oldest, oldest_token] = retired_order_.front();
      retired_order_.pop_front();
      const auto current = retired_.find(oldest);
      if (current != retired_.end() && SameToken(current->second, oldest_token)) {
        retired_.erase(current);
        ++diagnostics_.retired_tombstone_evictions;
      }
    }
  }

  void ReplayFallback() {
    PreviewBackend* destination = backend();
    if (destination == nullptr) return;
    if (attached_) {
      if (destination->Attach(target_) != Status::kOk) return;
    }
    for (const auto& [id, state] : strokes_) {
      (void)id;
      if (state.snapshot.stage == StrokeStage::kCancelled ||
          state.snapshot.stage == StrokeStage::kRetired) {
        continue;
      }
      if (destination->Begin(state.begin.value) != Status::kOk) continue;
      arc_preview_update_v0 update{
          .struct_size = sizeof(arc_preview_update_v0),
          .abi_version = ARC_ABI_VERSION,
          .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
          .coordinate_space = state.begin.value.coordinate_space,
          .stroke_id = state.snapshot.stroke_id,
          .preview_revision = state.snapshot.preview_revision,
          .view_id = state.begin.value.view_id,
          .viewport_revision = state.begin.value.viewport_revision,
          .target_generation = state.snapshot.target_generation,
          .source_to_device = state.begin.value.source_to_device,
          .truncate_confirmed_to = 0,
          .confirmed_append = state.snapshot.confirmed.data(),
          .confirmed_append_count =
              static_cast<uint32_t>(state.snapshot.confirmed.size()),
          .confirmed_append_stride = sizeof(arc_preview_primitive_v0),
          .predicted_tail = state.snapshot.predicted.data(),
          .predicted_tail_count =
              static_cast<uint32_t>(state.snapshot.predicted.size()),
          .predicted_tail_stride = sizeof(arc_preview_primitive_v0)};
      if (state.snapshot.preview_revision != 0) (void)destination->Push(update);
      if (state.snapshot.stage != StrokeStage::kActive) {
        arc_preview_seal_v0 seal{.struct_size = sizeof(arc_preview_seal_v0),
                                 .abi_version = ARC_ABI_VERSION,
                                 .stroke_id = state.snapshot.stroke_id,
                                 .final_preview_revision =
                                     state.snapshot.preview_revision,
                                 .target_generation =
                                     state.snapshot.target_generation};
        (void)destination->SealInput(seal);
      }
      if (state.snapshot.stage == StrokeStage::kAwaitingCanonical ||
          state.snapshot.stage == StrokeStage::kRecovering) {
        arc_canonical_commit_v0 commit{
            .struct_size = sizeof(arc_canonical_commit_v0),
            .abi_version = ARC_ABI_VERSION,
            .stroke_id = state.snapshot.stroke_id,
            .final_preview_revision = state.snapshot.preview_revision,
            .document_revision = state.snapshot.document_revision,
            .target_generation = state.snapshot.target_generation,
            .handoff_token = state.snapshot.handoff_token};
        (void)destination->CanonicalCommitted(commit);
      }
    }
  }

  size_t TotalBytes() const {
    size_t total = 0;
    for (const auto& [id, state] : strokes_) {
      (void)id;
      total += (state.snapshot.confirmed.size() +
                state.snapshot.predicted.size()) *
               sizeof(arc_preview_primitive_v0);
    }
    return total;
  }

  std::unique_ptr<PreviewBackend> primary_;
  std::unique_ptr<PreviewBackend> fallback_;
  BridgeLimits limits_;
  bool use_fallback_ = false;
  bool attached_ = false;
  bool canonical_redraw_requested_ = false;
  arc_preview_target_v0 target_{};
  std::map<uint64_t, StrokeState> strokes_;
  std::map<uint64_t, arc_handoff_token_v0> retired_;
  std::deque<std::pair<uint64_t, arc_handoff_token_v0>> retired_order_;
  Diagnostics diagnostics_;
  std::vector<TraceEvent> trace_;
};

Bridge::Bridge(std::unique_ptr<PreviewBackend> primary,
               std::unique_ptr<PreviewBackend> fallback, BridgeLimits limits)
    : impl_(std::make_unique<Impl>(std::move(primary), std::move(fallback),
                                  limits)) {}

Bridge::~Bridge() = default;

Status Bridge::Attach(const arc_preview_target_v0& target) {
  if (!ValidHeader(target) || target.target_id == 0 ||
      target.target_generation == 0 || target.width_pixels == 0 ||
      target.height_pixels == 0 || target.device_pixel_ratio <= 0.0F) {
    return Status::kInvalidArgument;
  }
  if (impl_->attached_ && target.target_generation < impl_->target_.target_generation) {
    return Status::kStaleRevision;
  }
  const bool replacing =
      impl_->attached_ && target.target_generation > impl_->target_.target_generation;
  impl_->target_ = target;
  impl_->attached_ = true;
  if (replacing) {
    for (auto& [id, state] : impl_->strokes_) {
      (void)id;
      state.snapshot.target_generation = target.target_generation;
      state.begin.value.target_generation = target.target_generation;
      if (state.snapshot.stage == StrokeStage::kRecovering) {
        state.snapshot.stage = StrokeStage::kAwaitingCanonical;
      }
    }
  }
  const Status result = impl_->Invoke(impl_->backend()->Attach(target), "attach");
  if (replacing) impl_->ReplayFallback();
  return result;
}

Status Bridge::Detach(uint64_t target_generation) {
  if (!impl_->attached_ || target_generation != impl_->target_.target_generation) {
    return Status::kStaleRevision;
  }
  const Status result = impl_->Invoke(impl_->backend()->Detach(target_generation),
                                      "detach");
  impl_->attached_ = false;
  return result;
}

Status Bridge::Begin(const arc_preview_begin_v0& begin) {
  if (!ValidHeader(begin) || begin.schema_version != ARC_PROTOCOL_SCHEMA_VERSION ||
      begin.stroke_id == 0 || begin.view_id == 0 ||
      begin.target_generation == 0 || !ValidHeader(begin.brush)) {
    return Status::kInvalidArgument;
  }
  if (!impl_->attached_ || begin.target_generation != impl_->target_.target_generation) {
    return Status::kStaleRevision;
  }
  auto existing = impl_->strokes_.find(begin.stroke_id);
  if (existing != impl_->strokes_.end()) {
    if (SameBegin(existing->second, begin)) return Status::kOk;
    ++impl_->diagnostics_.begin_collisions;
    return Status::kInvalidState;
  }
  if (impl_->retired_.contains(begin.stroke_id)) return Status::kInvalidState;
  const bool suppress_new =
      impl_->strokes_.size() >= impl_->limits_.max_pending_strokes;
  if (suppress_new) {
    impl_->DegradeForCapacity("begin_capacity");
    impl_->DropPreviewGeometry();
  }
  StrokeState state;
  state.snapshot.stroke_id = begin.stroke_id;
  state.snapshot.stage = StrokeStage::kActive;
  state.snapshot.target_generation = begin.target_generation;
  state.snapshot.preview_suppressed = suppress_new;
  state.begin.value = begin;
  if (begin.brush.resource_id_size != 0) {
    if (begin.brush.resource_id == nullptr) return Status::kInvalidArgument;
    state.begin.resource_id.assign(begin.brush.resource_id,
                                   begin.brush.resource_id_size);
  }
  if (begin.brush.resource_content_hash_size != 0) {
    if (begin.brush.resource_content_hash == nullptr) {
      return Status::kInvalidArgument;
    }
    state.begin.resource_hash.assign(begin.brush.resource_content_hash,
                                     begin.brush.resource_content_hash_size);
  }
  state.begin.value.brush.resource_id = state.begin.resource_id.data();
  state.begin.value.brush.resource_content_hash = state.begin.resource_hash.data();
  auto [iterator, inserted] = impl_->strokes_.emplace(begin.stroke_id,
                                                       std::move(state));
  if (!inserted) return Status::kInternalError;
  iterator->second.begin.value.brush.resource_id =
      iterator->second.begin.resource_id.data();
  iterator->second.begin.value.brush.resource_content_hash =
      iterator->second.begin.resource_hash.data();
  const Status result = impl_->Invoke(impl_->backend()->Begin(iterator->second.begin.value),
                                      "begin");
  ++impl_->diagnostics_.begun_strokes;
  impl_->trace_.push_back({.type = TraceEventType::kBegin,
                           .stroke_id = begin.stroke_id});
  return result;
}

Status Bridge::Push(const arc_preview_update_v0& update) {
  if (!ValidHeader(update) ||
      update.schema_version != ARC_PROTOCOL_SCHEMA_VERSION ||
      update.stroke_id == 0 || update.preview_revision == 0 ||
      !ValidStride(update.confirmed_append_count,
                   update.confirmed_append_stride,
                   sizeof(arc_preview_primitive_v0), update.confirmed_append) ||
      !ValidStride(update.predicted_tail_count, update.predicted_tail_stride,
                   sizeof(arc_preview_primitive_v0), update.predicted_tail)) {
    return Status::kInvalidArgument;
  }
  auto iterator = impl_->strokes_.find(update.stroke_id);
  if (iterator == impl_->strokes_.end()) return Status::kNotFound;
  StrokeSnapshot& state = iterator->second.snapshot;
  if (state.stage != StrokeStage::kActive) return Status::kInvalidState;
  if (update.target_generation != state.target_generation) {
    return Status::kStaleRevision;
  }
  if (update.preview_revision <= state.preview_revision) {
    ++impl_->diagnostics_.coalesced_or_stale_updates;
    return Status::kOk;
  }
  if (state.preview_suppressed) {
    state.preview_revision = update.preview_revision;
    ++impl_->diagnostics_.accepted_updates;
    impl_->trace_.push_back({.type = TraceEventType::kUpdate,
                             .stroke_id = update.stroke_id,
                             .preview_revision = update.preview_revision});
    return Status::kOk;
  }
  if (update.truncate_confirmed_to > state.confirmed.size()) {
    return Status::kInvalidArgument;
  }
  const size_t new_count = static_cast<size_t>(update.truncate_confirmed_to) +
                           update.confirmed_append_count +
                           update.predicted_tail_count;
  if (new_count > impl_->limits_.max_primitives_per_stroke) {
    impl_->DegradeForCapacity("stroke_capacity");
    impl_->DropPreviewGeometry();
    state.preview_revision = update.preview_revision;
    ++impl_->diagnostics_.accepted_updates;
    impl_->trace_.push_back({.type = TraceEventType::kUpdate,
                             .stroke_id = update.stroke_id,
                             .preview_revision = update.preview_revision});
    return Status::kOk;
  }
  const size_t old_bytes =
      (state.confirmed.size() + state.predicted.size()) *
      sizeof(arc_preview_primitive_v0);
  const size_t new_bytes = new_count * sizeof(arc_preview_primitive_v0);
  if (impl_->TotalBytes() - old_bytes + new_bytes > impl_->limits_.max_total_bytes) {
    impl_->DegradeForCapacity("queue_capacity");
    impl_->DropPreviewGeometry();
    state.preview_revision = update.preview_revision;
    ++impl_->diagnostics_.accepted_updates;
    impl_->trace_.push_back({.type = TraceEventType::kUpdate,
                             .stroke_id = update.stroke_id,
                             .preview_revision = update.preview_revision});
    return Status::kOk;
  }
  std::vector<arc_preview_primitive_v0> confirmed = state.confirmed;
  confirmed.resize(update.truncate_confirmed_to);
  for (uint32_t i = 0; i < update.confirmed_append_count; ++i) {
    confirmed.push_back(*PrimitiveAt(update.confirmed_append,
                                     update.confirmed_append_stride, i));
  }
  std::vector<arc_preview_primitive_v0> predicted;
  predicted.reserve(update.predicted_tail_count);
  for (uint32_t i = 0; i < update.predicted_tail_count; ++i) {
    predicted.push_back(*PrimitiveAt(update.predicted_tail,
                                     update.predicted_tail_stride, i));
  }
  state.confirmed = std::move(confirmed);
  state.predicted = std::move(predicted);
  state.preview_revision = update.preview_revision;
  ++impl_->diagnostics_.accepted_updates;
  impl_->trace_.push_back({.type = TraceEventType::kUpdate,
                           .stroke_id = update.stroke_id,
                           .preview_revision = update.preview_revision});
  arc_preview_update_v0 owned_update = update;
  owned_update.truncate_confirmed_to = 0;
  owned_update.confirmed_append = state.confirmed.data();
  owned_update.confirmed_append_count =
      static_cast<uint32_t>(state.confirmed.size());
  owned_update.confirmed_append_stride = sizeof(arc_preview_primitive_v0);
  owned_update.predicted_tail = state.predicted.data();
  owned_update.predicted_tail_count =
      static_cast<uint32_t>(state.predicted.size());
  owned_update.predicted_tail_stride = sizeof(arc_preview_primitive_v0);
  return impl_->Invoke(impl_->backend()->Push(owned_update), "push");
}

Status Bridge::SealInput(const arc_preview_seal_v0& seal) {
  if (!ValidHeader(seal) || seal.stroke_id == 0 || seal.final_preview_revision == 0) {
    return Status::kInvalidArgument;
  }
  auto iterator = impl_->strokes_.find(seal.stroke_id);
  if (iterator == impl_->strokes_.end()) return Status::kNotFound;
  StrokeSnapshot& state = iterator->second.snapshot;
  if (state.stage == StrokeStage::kSealed ||
      state.stage == StrokeStage::kAwaitingCanonical) {
    return seal.final_preview_revision == state.preview_revision
               ? Status::kOk
               : Status::kInvalidState;
  }
  if (state.stage != StrokeStage::kActive ||
      seal.final_preview_revision != state.preview_revision ||
      seal.target_generation != state.target_generation) {
    return Status::kInvalidState;
  }
  state.stage = StrokeStage::kSealed;
  state.predicted.clear();
  ++impl_->diagnostics_.sealed_strokes;
  return impl_->Invoke(impl_->backend()->SealInput(seal), "seal_input");
}

Status Bridge::CanonicalCommitted(const arc_canonical_commit_v0& commit) {
  if (!ValidHeader(commit) || commit.stroke_id == 0 ||
      commit.document_revision == 0 || !NonZeroToken(commit.handoff_token)) {
    return Status::kInvalidArgument;
  }
  auto iterator = impl_->strokes_.find(commit.stroke_id);
  if (iterator == impl_->strokes_.end()) return Status::kNotFound;
  StrokeSnapshot& state = iterator->second.snapshot;
  if (state.stage == StrokeStage::kAwaitingCanonical) {
    return state.document_revision == commit.document_revision &&
                   SameToken(state.handoff_token, commit.handoff_token)
               ? Status::kOk
               : Status::kInvalidState;
  }
  if (state.stage != StrokeStage::kSealed ||
      commit.final_preview_revision != state.preview_revision ||
      commit.target_generation != state.target_generation) {
    return Status::kInvalidState;
  }
  state.stage = StrokeStage::kAwaitingCanonical;
  state.document_revision = commit.document_revision;
  state.handoff_token = commit.handoff_token;
  ++impl_->diagnostics_.canonical_commits;
  impl_->trace_.push_back({.type = TraceEventType::kCanonicalCommitted,
                           .stroke_id = commit.stroke_id,
                           .preview_revision = commit.final_preview_revision,
                           .document_revision = commit.document_revision});
  return impl_->Invoke(impl_->backend()->CanonicalCommitted(commit),
                       "canonical_committed");
}

Status Bridge::CanonicalVisible(const arc_canonical_visible_v0& visible) {
  if (!ValidHeader(visible) || !ValidHeader(visible.receipt) ||
      visible.stroke_id == 0 || visible.document_revision == 0 ||
      !NonZeroToken(visible.handoff_token)) {
    return Status::kInvalidArgument;
  }
  auto retired = impl_->retired_.find(visible.stroke_id);
  if (retired != impl_->retired_.end()) {
    return SameToken(retired->second, visible.handoff_token) ? Status::kOk
                                                             : Status::kInvalidState;
  }
  auto iterator = impl_->strokes_.find(visible.stroke_id);
  if (iterator == impl_->strokes_.end()) return Status::kNotFound;
  const StrokeSnapshot& state = iterator->second.snapshot;
  if ((state.stage != StrokeStage::kAwaitingCanonical &&
       state.stage != StrokeStage::kRecovering) ||
      state.document_revision != visible.document_revision ||
      state.target_generation != visible.target_generation ||
      !SameToken(state.handoff_token, visible.handoff_token)) {
    ++impl_->diagnostics_.stale_acknowledgements;
    return Status::kStaleRevision;
  }
  if (visible.receipt.target_generation != visible.target_generation ||
      visible.receipt.status != ARC_STATUS_OK ||
      (visible.receipt.evidence < ARC_EVIDENCE_COMPOSITOR_VISIBLE &&
       visible.receipt.evidence != ARC_EVIDENCE_DETERMINISTIC_ORACLE)) {
    return Status::kInvalidArgument;
  }
  PreviewBackend* attempted_backend = impl_->backend();
  const Status backend_status = attempted_backend->CanonicalVisible(visible);
  (void)impl_->Invoke(backend_status, "canonical_visible");
  if (backend_status != Status::kOk && impl_->backend() != attempted_backend) {
    (void)impl_->backend()->CanonicalVisible(visible);
  }
  const uint64_t final_preview_revision = state.preview_revision;
  impl_->RememberRetired(visible.stroke_id, visible.handoff_token);
  impl_->strokes_.erase(iterator);
  ++impl_->diagnostics_.retired_strokes;
  ++impl_->diagnostics_.canonical_visible;
  impl_->trace_.push_back({.type = TraceEventType::kCanonicalVisible,
                           .stroke_id = visible.stroke_id,
                           .preview_revision = final_preview_revision,
                           .document_revision = visible.document_revision});
  return Status::kOk;
}

Status Bridge::Cancel(const arc_preview_cancel_v0& cancel) {
  if (!ValidHeader(cancel) || cancel.stroke_id == 0) {
    return Status::kInvalidArgument;
  }
  auto iterator = impl_->strokes_.find(cancel.stroke_id);
  if (iterator == impl_->strokes_.end()) {
    return impl_->retired_.contains(cancel.stroke_id) ? Status::kInvalidState
                                                       : Status::kOk;
  }
  if (cancel.target_generation != iterator->second.snapshot.target_generation) {
    return Status::kStaleRevision;
  }
  const Status backend_status = impl_->backend()->Cancel(cancel);
  (void)impl_->Invoke(backend_status, "cancel");
  impl_->strokes_.erase(iterator);
  ++impl_->diagnostics_.cancelled_strokes;
  impl_->trace_.push_back({.type = TraceEventType::kCancel,
                           .stroke_id = cancel.stroke_id});
  return Status::kOk;
}

void Bridge::SurfaceLost(uint64_t target_generation) {
  if (!impl_->attached_ || target_generation != impl_->target_.target_generation) {
    return;
  }
  for (auto& [id, state] : impl_->strokes_) {
    (void)id;
    if (state.snapshot.stage == StrokeStage::kAwaitingCanonical) {
      state.snapshot.stage = StrokeStage::kRecovering;
    }
  }
  ++impl_->diagnostics_.backend_failures;
  impl_->diagnostics_.last_backend_error = Status::kSurfaceLost;
  impl_->diagnostics_.last_backend_message = "surface_lost";
  impl_->canonical_redraw_requested_ = true;
  if (!impl_->use_fallback_) {
    impl_->use_fallback_ = true;
    ++impl_->diagnostics_.fallback_activations;
    impl_->ReplayFallback();
  }
}

bool Bridge::using_fallback() const { return impl_->use_fallback_; }

bool Bridge::TakeCanonicalRedrawRequest() {
  const bool requested = impl_->canonical_redraw_requested_;
  impl_->canonical_redraw_requested_ = false;
  return requested;
}

const Diagnostics& Bridge::diagnostics() const { return impl_->diagnostics_; }

const std::vector<TraceEvent>& Bridge::trace() const { return impl_->trace_; }

std::vector<uint64_t> Bridge::StrokeIds() const {
  std::vector<uint64_t> ids;
  ids.reserve(impl_->strokes_.size());
  for (const auto& [id, state] : impl_->strokes_) {
    (void)state;
    ids.push_back(id);
  }
  return ids;
}

const StrokeSnapshot* Bridge::Find(uint64_t stroke_id) const {
  const auto iterator = impl_->strokes_.find(stroke_id);
  return iterator == impl_->strokes_.end() ? nullptr
                                           : &iterator->second.snapshot;
}

std::unique_ptr<PreviewBackend> CreateNullBackend() {
  return std::make_unique<NullBackend>();
}

}  // namespace arc
