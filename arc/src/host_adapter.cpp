#include "arc/host_adapter.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace arc {
namespace {

template <typename T>
bool ValidHeader(const T& value) {
  return value.struct_size >= sizeof(T) && value.abi_version == ARC_ABI_VERSION;
}

bool ValidCoordinateSpace(uint32_t value) {
  return value == ARC_COORDINATE_SPACE_WORLD ||
         value == ARC_COORDINATE_SPACE_VIEW_LOGICAL ||
         value == ARC_COORDINATE_SPACE_DEVICE_PIXEL;
}

bool ValidTool(uint32_t value) {
  return value == ARC_INPUT_TOOL_MOUSE || value == ARC_INPUT_TOOL_PEN ||
         value == ARC_INPUT_TOOL_TOUCH;
}

bool ValidPhase(uint32_t value) {
  return value == ARC_POINTER_PHASE_DOWN || value == ARC_POINTER_PHASE_MOVE ||
         value == ARC_POINTER_PHASE_UP || value == ARC_POINTER_PHASE_CANCEL ||
         value == ARC_POINTER_PHASE_HOVER;
}

bool ValidProvenance(uint32_t value) {
  return value == ARC_SAMPLE_CONFIRMED_CURRENT ||
         value == ARC_SAMPLE_CONFIRMED_COALESCED ||
         value == ARC_SAMPLE_PLATFORM_PREDICTION_HINT;
}

bool Finite(float value) { return std::isfinite(value); }

bool ValidTransform(const arc_affine_transform_v0& transform) {
  return Finite(transform.m00) && Finite(transform.m01) &&
         Finite(transform.m10) && Finite(transform.m11) &&
         Finite(transform.tx) && Finite(transform.ty);
}

arc_pointer_sample_v0 SampleAt(const arc_pointer_sample_batch_v0& batch,
                               uint32_t index) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(batch.samples);
  arc_pointer_sample_v0 sample{};
  std::memcpy(&sample,
              bytes + static_cast<size_t>(batch.sample_stride) * index,
              sizeof(sample));
  return sample;
}

bool ValidSample(const arc_pointer_sample_v0& sample) {
  return sample.pointer_id != 0 && ValidPhase(sample.phase) &&
         ValidProvenance(sample.provenance) && Finite(sample.x) &&
         Finite(sample.y) && Finite(sample.pressure) && sample.pressure >= 0.0F &&
         sample.pressure <= 1.0F && Finite(sample.tilt_x) &&
         Finite(sample.tilt_y) && Finite(sample.contact_width) &&
         sample.contact_width >= 0.0F && Finite(sample.contact_height) &&
         sample.contact_height >= 0.0F;
}

struct PointerKey {
  uint64_t device_id = 0;
  uint64_t pointer_id = 0;

  friend bool operator<(const PointerKey& left, const PointerKey& right) {
    return left.device_id < right.device_id ||
           (left.device_id == right.device_id &&
            left.pointer_id < right.pointer_id);
  }
};

struct SampleCursor {
  uint64_t sequence = 0;
  uint64_t timestamp_us = 0;
};

}  // namespace

class HostAdapter::Impl {
 public:
  Impl(std::unique_ptr<PreviewBackend> primary,
       std::unique_ptr<PreviewBackend> fallback,
       std::unique_ptr<InputSource> input_source, BridgeLimits limits)
      : bridge(std::move(primary), std::move(fallback), limits),
        input(std::move(input_source)) {}

  Bridge bridge;
  std::unique_ptr<InputSource> input;
  PointerSampleSink* downstream = nullptr;
  bool target_attached = false;
  bool input_running = false;
  bool surface_lost = false;
  arc_preview_target_v0 target{};
  std::map<PointerKey, SampleCursor> cursors;
};

HostAdapter::HostAdapter(std::unique_ptr<PreviewBackend> primary,
                         std::unique_ptr<PreviewBackend> fallback,
                         std::unique_ptr<InputSource> input_source,
                         BridgeLimits limits)
    : impl_(std::make_unique<Impl>(std::move(primary), std::move(fallback),
                                   std::move(input_source), limits)) {}

HostAdapter::~HostAdapter() {
  if (impl_->input_running && impl_->input != nullptr) {
    (void)impl_->input->Stop();
  }
  if (impl_->target_attached) {
    (void)impl_->bridge.Detach(impl_->target.target_generation);
  }
}

Status HostAdapter::AttachTarget(const arc_preview_target_v0& target) {
  if (impl_->target_attached) {
    if (target.target_id != impl_->target.target_id) {
      return Status::kInvalidArgument;
    }
    if (target.target_generation <= impl_->target.target_generation) {
      return target.target_generation < impl_->target.target_generation
                 ? Status::kStaleRevision
                 : Status::kInvalidState;
    }
  }
  const Status status = impl_->bridge.Attach(target);
  if (status != Status::kOk) return status;
  impl_->target = target;
  impl_->target_attached = true;
  impl_->surface_lost = false;
  return Status::kOk;
}

Status HostAdapter::ResizeTarget(const arc_preview_target_v0& target) {
  if (!impl_->target_attached) return Status::kInvalidState;
  if (target.target_id != impl_->target.target_id) {
    return Status::kInvalidArgument;
  }
  if (target.target_generation != impl_->target.target_generation ||
      impl_->surface_lost) {
    return Status::kStaleRevision;
  }
  const Status status = impl_->bridge.Attach(target);
  if (status != Status::kOk) return status;
  impl_->target = target;
  impl_->surface_lost = false;
  return Status::kOk;
}

Status HostAdapter::DetachTarget(uint64_t target_generation) {
  if (!impl_->target_attached) return Status::kInvalidState;
  const Status status = impl_->bridge.Detach(target_generation);
  if (status != Status::kOk) return status;
  impl_->target_attached = false;
  impl_->surface_lost = false;
  impl_->target = {};
  return Status::kOk;
}

void HostAdapter::SurfaceLost(uint64_t target_generation) {
  if (!impl_->target_attached ||
      target_generation != impl_->target.target_generation) {
    return;
  }
  impl_->surface_lost = true;
  impl_->bridge.SurfaceLost(target_generation);
}

Status HostAdapter::StartInput(PointerSampleSink& sink) {
  if (impl_->input_running) return Status::kInvalidState;
  if (impl_->input == nullptr) return Status::kBackendUnavailable;
  impl_->downstream = &sink;
  const Status status = impl_->input->Start(*this);
  if (status != Status::kOk) {
    impl_->downstream = nullptr;
    return status;
  }
  impl_->input_running = true;
  impl_->cursors.clear();
  return Status::kOk;
}

Status HostAdapter::StopInput() {
  if (!impl_->input_running) return Status::kOk;
  if (impl_->input == nullptr) return Status::kBackendUnavailable;
  const Status status = impl_->input->Stop();
  if (status != Status::kOk) return status;
  impl_->input_running = false;
  impl_->downstream = nullptr;
  impl_->cursors.clear();
  return Status::kOk;
}

Status HostAdapter::SubmitPointerBatch(
    const arc_pointer_sample_batch_v0& batch) {
  if (!impl_->input_running || impl_->input == nullptr) {
    return Status::kInvalidState;
  }
  return impl_->input->SubmitBatch(batch);
}

void HostAdapter::NotifyInputLost(Status reason) {
  if (!impl_->input_running || impl_->input == nullptr) return;
  impl_->input->NotifySourceLost(reason);
}

Status HostAdapter::CanonicalVisible(
    const arc_canonical_visible_v0& visible) {
  if (!impl_->target_attached) return Status::kInvalidState;
  if (visible.target_generation != impl_->target.target_generation) {
    return Status::kStaleRevision;
  }
  return impl_->bridge.CanonicalVisible(visible);
}

bool HostAdapter::target_attached() const { return impl_->target_attached; }

bool HostAdapter::input_running() const { return impl_->input_running; }

uint64_t HostAdapter::target_generation() const {
  return impl_->target_attached ? impl_->target.target_generation : 0;
}

bool HostAdapter::TakeCanonicalRedrawRequest() {
  return impl_->bridge.TakeCanonicalRedrawRequest();
}

Bridge& HostAdapter::bridge() { return impl_->bridge; }

const Bridge& HostAdapter::bridge() const { return impl_->bridge; }

Status HostAdapter::Push(const arc_pointer_sample_batch_v0& batch) {
  if (!impl_->input_running || impl_->downstream == nullptr) {
    return Status::kInvalidState;
  }
  if (!ValidHeader(batch) ||
      batch.schema_version != ARC_PROTOCOL_SCHEMA_VERSION ||
      !ValidCoordinateSpace(batch.coordinate_space) || batch.view_id == 0 ||
      batch.viewport_revision == 0 || batch.device_id == 0 ||
      !ValidTool(batch.tool) || !ValidTransform(batch.view_to_world) ||
      batch.samples == nullptr || batch.sample_count == 0 ||
      batch.sample_stride < sizeof(arc_pointer_sample_v0) ||
      batch.sample_count >
          std::numeric_limits<size_t>::max() / batch.sample_stride) {
    return Status::kInvalidArgument;
  }

  auto next_cursors = impl_->cursors;
  for (uint32_t index = 0; index < batch.sample_count; ++index) {
    const arc_pointer_sample_v0 sample = SampleAt(batch, index);
    if (!ValidSample(sample)) return Status::kInvalidArgument;
    const PointerKey key{.device_id = batch.device_id,
                         .pointer_id = sample.pointer_id};
    const auto previous = next_cursors.find(key);
    if (previous != next_cursors.end() &&
        (sample.sample_sequence <= previous->second.sequence ||
         sample.timestamp_us < previous->second.timestamp_us)) {
      return Status::kStaleRevision;
    }
    if (sample.phase == ARC_POINTER_PHASE_UP ||
        sample.phase == ARC_POINTER_PHASE_CANCEL) {
      next_cursors.erase(key);
    } else {
      next_cursors[key] = {.sequence = sample.sample_sequence,
                           .timestamp_us = sample.timestamp_us};
    }
  }

  const Status status = impl_->downstream->Push(batch);
  if (status == Status::kOk) impl_->cursors = std::move(next_cursors);
  return status;
}

void HostAdapter::SourceLost(uint64_t device_id, Status reason) {
  for (auto iterator = impl_->cursors.begin(); iterator != impl_->cursors.end();) {
    if (device_id == 0 || iterator->first.device_id == device_id) {
      iterator = impl_->cursors.erase(iterator);
    } else {
      ++iterator;
    }
  }
  if (impl_->input_running && impl_->downstream != nullptr) {
    impl_->downstream->SourceLost(device_id, reason);
  }
}

}  // namespace arc
