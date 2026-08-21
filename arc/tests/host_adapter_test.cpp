#include "arc/host_adapter.hpp"

#include <cassert>
#include <cstdint>
#include <memory>

namespace {

using arc::HostAdapter;
using arc::PointerSampleSink;
using arc::Status;

struct DirectInputSource final : arc::InputSource {
  arc_backend_capabilities_v0 Capabilities() const override {
    return {.struct_size = sizeof(arc_backend_capabilities_v0),
            .abi_version = ARC_ABI_VERSION,
            .platform_kind = ARC_PLATFORM_HEADLESS,
            .input_capabilities = ARC_INPUT_CAPABILITY_PRESSURE};
  }

  Status Start(PointerSampleSink& target) override {
    if (sink != nullptr) return Status::kInvalidState;
    sink = &target;
    return Status::kOk;
  }

  Status Stop() override {
    sink = nullptr;
    return Status::kOk;
  }

  Status SubmitBatch(const arc_pointer_sample_batch_v0& batch) override {
    if (sink == nullptr) return Status::kInvalidState;
    device_id = batch.device_id;
    return sink->Push(batch);
  }

  void NotifySourceLost(Status reason) override {
    if (sink != nullptr) sink->SourceLost(device_id, reason);
  }

  PointerSampleSink* sink = nullptr;
  uint64_t device_id = 0;
};

std::unique_ptr<arc::InputSource> MakeInputSource() {
  return std::make_unique<DirectInputSource>();
}

arc_preview_target_v0 Target(uint64_t generation = 1) {
  return {.struct_size = sizeof(arc_preview_target_v0),
          .abi_version = ARC_ABI_VERSION,
          .platform_kind = ARC_PLATFORM_HEADLESS,
          .target_id = 91,
          .target_generation = generation,
          .width_pixels = 800,
          .height_pixels = 600,
          .device_pixel_ratio = 1.0F};
}

struct RecordingSink final : PointerSampleSink {
  Status Push(const arc_pointer_sample_batch_v0& batch) override {
    ++batches;
    last_sequence = batch.samples[batch.sample_count - 1].sample_sequence;
    return push_status;
  }

  void SourceLost(uint64_t device_id, Status reason) override {
    ++source_losses;
    lost_device = device_id;
    lost_reason = reason;
  }

  Status push_status = Status::kOk;
  uint32_t batches = 0;
  uint32_t source_losses = 0;
  uint64_t last_sequence = 0;
  uint64_t lost_device = 0;
  Status lost_reason = Status::kOk;
};

arc_pointer_sample_batch_v0 Batch(arc_pointer_sample_v0* samples,
                                  uint32_t count = 1) {
  return {.struct_size = sizeof(arc_pointer_sample_batch_v0),
          .abi_version = ARC_ABI_VERSION,
          .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
          .coordinate_space = ARC_COORDINATE_SPACE_VIEW_LOGICAL,
          .view_id = 3,
          .viewport_revision = 4,
          .device_id = 8,
          .input_capabilities = ARC_INPUT_CAPABILITY_PRESSURE,
          .tool = ARC_INPUT_TOOL_PEN,
          .view_to_world = {.m00 = 1.0F, .m11 = 1.0F},
          .samples = samples,
          .sample_count = count,
          .sample_stride = sizeof(arc_pointer_sample_v0)};
}

arc_pointer_sample_v0 Sample(uint64_t sequence, uint32_t phase) {
  return {.pointer_id = 12,
          .sample_sequence = sequence,
          .timestamp_us = sequence * 100,
          .x = 10.0F,
          .y = 20.0F,
          .pressure = 0.5F,
          .phase = phase,
          .provenance = ARC_SAMPLE_CONFIRMED_CURRENT};
}

void TestTargetLifecycleAndGeneration() {
  HostAdapter host(arc::CreateNullBackend(), arc::CreateNullBackend(),
                   MakeInputSource());
  assert(!host.target_attached());
  assert(host.AttachTarget(Target()) == Status::kOk);
  assert(host.target_attached() && host.target_generation() == 1);
  assert(host.AttachTarget(Target()) == Status::kInvalidState);
  auto resized = Target();
  resized.width_pixels = 1024;
  resized.height_pixels = 768;
  assert(host.ResizeTarget(resized) == Status::kOk);
  auto replacement = Target(2);
  assert(host.ResizeTarget(replacement) == Status::kStaleRevision);
  assert(host.AttachTarget(replacement) == Status::kOk);
  assert(host.target_generation() == 2);
  assert(host.DetachTarget(1) == Status::kStaleRevision);
  assert(host.DetachTarget(2) == Status::kOk);
  assert(!host.target_attached());
  assert(host.ResizeTarget(replacement) == Status::kInvalidState);
}

void TestBatchValidationAndInputLifecycle() {
  HostAdapter host(arc::CreateNullBackend(), arc::CreateNullBackend(),
                   MakeInputSource());
  RecordingSink sink;
  arc_pointer_sample_v0 down = Sample(1, ARC_POINTER_PHASE_DOWN);
  auto batch = Batch(&down);
  assert(host.SubmitPointerBatch(batch) == Status::kInvalidState);
  assert(host.StartInput(sink) == Status::kOk);
  assert(host.StartInput(sink) == Status::kInvalidState);
  assert(host.SubmitPointerBatch(batch) == Status::kOk);
  assert(sink.batches == 1 && sink.last_sequence == 1);

  arc_pointer_sample_v0 stale = Sample(1, ARC_POINTER_PHASE_MOVE);
  assert(host.SubmitPointerBatch(Batch(&stale)) == Status::kStaleRevision);
  arc_pointer_sample_v0 move = Sample(2, ARC_POINTER_PHASE_MOVE);
  assert(host.SubmitPointerBatch(Batch(&move)) == Status::kOk);
  arc_pointer_sample_v0 invalid = move;
  invalid.pressure = 2.0F;
  assert(host.SubmitPointerBatch(Batch(&invalid)) == Status::kInvalidArgument);
  arc_pointer_sample_v0 up = Sample(3, ARC_POINTER_PHASE_UP);
  assert(host.SubmitPointerBatch(Batch(&up)) == Status::kOk);
  arc_pointer_sample_v0 next_down = Sample(1, ARC_POINTER_PHASE_DOWN);
  assert(host.SubmitPointerBatch(Batch(&next_down)) == Status::kOk);

  host.NotifyInputLost(Status::kSurfaceLost);
  assert(sink.source_losses == 1 && sink.lost_device == 8);
  assert(sink.lost_reason == Status::kSurfaceLost);
  assert(host.SubmitPointerBatch(Batch(&next_down)) == Status::kOk);
  assert(host.StopInput() == Status::kOk);
  assert(!host.input_running());
  assert(host.StopInput() == Status::kOk);
}

void TestDownstreamFailureDoesNotAdvanceCursor() {
  HostAdapter host(arc::CreateNullBackend(), arc::CreateNullBackend(),
                   MakeInputSource());
  RecordingSink sink;
  assert(host.StartInput(sink) == Status::kOk);
  arc_pointer_sample_v0 down = Sample(1, ARC_POINTER_PHASE_DOWN);
  assert(host.SubmitPointerBatch(Batch(&down)) == Status::kOk);
  sink.push_status = Status::kInternalError;
  arc_pointer_sample_v0 failed = Sample(2, ARC_POINTER_PHASE_MOVE);
  assert(host.SubmitPointerBatch(Batch(&failed)) == Status::kInternalError);
  sink.push_status = Status::kOk;
  assert(host.SubmitPointerBatch(Batch(&failed)) == Status::kOk);
}

void TestSurfaceLossRequestsCanonicalRedraw() {
  HostAdapter host(arc::CreateNullBackend(), arc::CreateNullBackend(),
                   MakeInputSource());
  assert(host.AttachTarget(Target()) == Status::kOk);
  host.SurfaceLost(99);
  assert(!host.TakeCanonicalRedrawRequest());
  host.SurfaceLost(1);
  assert(host.TakeCanonicalRedrawRequest());
  assert(!host.TakeCanonicalRedrawRequest());
  assert(host.ResizeTarget(Target()) == Status::kStaleRevision);
  assert(host.AttachTarget(Target(2)) == Status::kOk);
}

}  // namespace

int main() {
  TestTargetLifecycleAndGeneration();
  TestBatchValidationAndInputLifecycle();
  TestDownstreamFailureDoesNotAdvanceCursor();
  TestSurfaceLossRequestsCanonicalRedraw();
  return 0;
}
