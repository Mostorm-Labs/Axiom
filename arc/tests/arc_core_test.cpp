#include "arc/arc.hpp"

#include <cassert>
#include <cstdint>
#include <memory>

namespace {

using arc::Bridge;
using arc::PreviewBackend;
using arc::Status;

struct RecordingBackend final : PreviewBackend {
  explicit RecordingBackend(bool fail_push = false) : fail_push(fail_push) {}

  arc_backend_capabilities_v0 Capabilities() const override {
    return {.struct_size = sizeof(arc_backend_capabilities_v0),
            .abi_version = ARC_ABI_VERSION,
            .platform_kind = ARC_PLATFORM_HEADLESS,
            .presentation_capabilities =
                ARC_PRESENTATION_CAPABILITY_INDEPENDENT_TARGET |
                ARC_PRESENTATION_CAPABILITY_REPLACE_TRUNCATE |
                ARC_PRESENTATION_CAPABILITY_PRESENT_RECEIPT,
            .max_pending_strokes = 8,
            .max_primitives_per_update = 64,
            .max_queue_bytes = 1 << 20};
  }
  Status Attach(const arc_preview_target_v0&) override { ++attaches; return Status::kOk; }
  Status Detach(uint64_t) override { ++detaches; return Status::kOk; }
  Status Begin(const arc_preview_begin_v0&) override { ++begins; return Status::kOk; }
  Status Push(const arc_preview_update_v0&) override {
    ++pushes;
    return fail_push ? Status::kPresentationFailed : Status::kOk;
  }
  Status SealInput(const arc_preview_seal_v0&) override { ++seals; return Status::kOk; }
  Status CanonicalCommitted(const arc_canonical_commit_v0&) override {
    ++commits;
    return Status::kOk;
  }
  Status CanonicalVisible(const arc_canonical_visible_v0&) override {
    ++visible;
    return Status::kOk;
  }
  Status Cancel(const arc_preview_cancel_v0&) override { ++cancels; return Status::kOk; }

  bool fail_push = false;
  uint32_t attaches = 0;
  uint32_t detaches = 0;
  uint32_t begins = 0;
  uint32_t pushes = 0;
  uint32_t seals = 0;
  uint32_t commits = 0;
  uint32_t visible = 0;
  uint32_t cancels = 0;
};

arc_preview_target_v0 Target() {
  return {.struct_size = sizeof(arc_preview_target_v0),
          .abi_version = ARC_ABI_VERSION,
          .platform_kind = ARC_PLATFORM_HEADLESS,
          .target_id = 1,
          .target_generation = 7,
          .width_pixels = 800,
          .height_pixels = 600,
          .device_pixel_ratio = 1.0F};
}

arc_brush_descriptor_v0 Brush() {
  return {.struct_size = sizeof(arc_brush_descriptor_v0),
          .abi_version = ARC_ABI_VERSION,
          .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
          .brush_type = ARC_PREVIEW_PRIMITIVE_VECTOR_POINT,
          .brush_version = 1,
          .algorithm_version = 1,
          .size = 4.0F,
          .spacing = 0.25F,
          .opacity = 1.0F};
}

arc_preview_begin_v0 Begin(uint64_t stroke_id) {
  return {.struct_size = sizeof(arc_preview_begin_v0),
          .abi_version = ARC_ABI_VERSION,
          .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
          .coordinate_space = ARC_COORDINATE_SPACE_WORLD,
          .stroke_id = stroke_id,
          .view_id = 1,
          .viewport_revision = 1,
          .target_generation = 7,
          .brush = Brush()};
}

arc_preview_update_v0 Update(uint64_t stroke_id, uint64_t revision,
                             arc_preview_primitive_v0* primitive) {
  return {.struct_size = sizeof(arc_preview_update_v0),
          .abi_version = ARC_ABI_VERSION,
          .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
          .coordinate_space = ARC_COORDINATE_SPACE_WORLD,
          .stroke_id = stroke_id,
          .preview_revision = revision,
          .view_id = 1,
          .viewport_revision = 1,
          .target_generation = 7,
          .truncate_confirmed_to = 0,
          .confirmed_append = primitive,
          .confirmed_append_count = 1,
          .confirmed_append_stride = sizeof(arc_preview_primitive_v0)};
}

arc_handoff_token_v0 Token(uint64_t id) { return {.high = 1, .low = id}; }

void TestLifecycleAndStaleAck() {
  auto primary = std::make_unique<RecordingBackend>();
  auto* primary_raw = primary.get();
  Bridge bridge(std::move(primary), arc::CreateNullBackend());
  assert(bridge.Attach(Target()) == Status::kOk);
  assert(bridge.Begin(Begin(11)) == Status::kOk);
  arc_preview_primitive_v0 primitive{.kind = ARC_PREVIEW_PRIMITIVE_VECTOR_POINT,
                                     .x = 10.0F,
                                     .y = 20.0F,
                                     .radius = 2.0F,
                                     .opacity = 1.0F};
  assert(bridge.Push(Update(11, 1, &primitive)) == Status::kOk);
  arc_preview_seal_v0 seal{.struct_size = sizeof(arc_preview_seal_v0),
                           .abi_version = ARC_ABI_VERSION,
                           .stroke_id = 11,
                           .final_preview_revision = 1,
                           .target_generation = 7};
  assert(bridge.SealInput(seal) == Status::kOk);
  arc_canonical_commit_v0 commit{.struct_size = sizeof(arc_canonical_commit_v0),
                                .abi_version = ARC_ABI_VERSION,
                                .stroke_id = 11,
                                .final_preview_revision = 1,
                                .document_revision = 2,
                                .target_generation = 7,
                                .handoff_token = Token(11)};
  assert(bridge.CanonicalCommitted(commit) == Status::kOk);
  arc_canonical_visible_v0 stale{.struct_size = sizeof(arc_canonical_visible_v0),
                                 .abi_version = ARC_ABI_VERSION,
                                 .stroke_id = 11,
                                 .document_revision = 1,
                                 .target_generation = 7,
                                 .handoff_token = Token(11),
                                 .receipt = {.struct_size = sizeof(arc_presentation_receipt_v0),
                                             .abi_version = ARC_ABI_VERSION,
                                             .evidence = ARC_EVIDENCE_COMPOSITOR_VISIBLE,
                                             .status = ARC_STATUS_OK,
                                             .target_generation = 7}};
  assert(bridge.CanonicalVisible(stale) == Status::kStaleRevision);
  arc_canonical_visible_v0 visible = stale;
  visible.document_revision = 2;
  assert(bridge.CanonicalVisible(visible) == Status::kOk);
  assert(bridge.Find(11) == nullptr);
  assert(primary_raw->begins == 1 && primary_raw->pushes == 1);
}

void TestPresentationFailureFallsBack() {
  auto primary = std::make_unique<RecordingBackend>(true);
  Bridge bridge(std::move(primary), arc::CreateNullBackend());
  assert(bridge.Attach(Target()) == Status::kOk);
  assert(bridge.Begin(Begin(12)) == Status::kOk);
  arc_preview_primitive_v0 primitive{.kind = ARC_PREVIEW_PRIMITIVE_DAB,
                                     .x = 1.0F,
                                     .y = 2.0F,
                                     .radius = 3.0F,
                                     .opacity = 1.0F};
  assert(bridge.Push(Update(12, 1, &primitive)) == Status::kOk);
  assert(bridge.using_fallback());
  assert(bridge.TakeCanonicalRedrawRequest());
  assert(!bridge.TakeCanonicalRedrawRequest());
  assert(bridge.Find(12) != nullptr);
  assert(bridge.diagnostics().fallback_activations == 1);
}

void TestMultiplePendingStrokesAndGenerationRecovery() {
  Bridge bridge(std::make_unique<RecordingBackend>(), arc::CreateNullBackend());
  assert(bridge.Attach(Target()) == Status::kOk);
  arc_preview_primitive_v0 primitive{.kind = ARC_PREVIEW_PRIMITIVE_VECTOR_POINT,
                                     .radius = 1.0F,
                                     .opacity = 1.0F};
  for (uint64_t id : {21u, 22u}) {
    assert(bridge.Begin(Begin(id)) == Status::kOk);
    assert(bridge.Push(Update(id, 1, &primitive)) == Status::kOk);
    arc_preview_seal_v0 seal{.struct_size = sizeof(arc_preview_seal_v0),
                             .abi_version = ARC_ABI_VERSION,
                             .stroke_id = id,
                             .final_preview_revision = 1,
                             .target_generation = 7};
    assert(bridge.SealInput(seal) == Status::kOk);
    arc_canonical_commit_v0 commit{
        .struct_size = sizeof(arc_canonical_commit_v0),
        .abi_version = ARC_ABI_VERSION,
        .stroke_id = id,
        .final_preview_revision = 1,
        .document_revision = id,
        .target_generation = 7,
        .handoff_token = Token(id)};
    assert(bridge.CanonicalCommitted(commit) == Status::kOk);
  }
  bridge.SurfaceLost(7);
  assert(bridge.Find(21)->stage == arc::StrokeStage::kRecovering);
  arc_preview_target_v0 replacement = Target();
  replacement.target_generation = 8;
  assert(bridge.Attach(replacement) == Status::kOk);
  assert(bridge.Find(21)->target_generation == 8);
  assert(bridge.Find(22)->target_generation == 8);
  arc_canonical_visible_v0 visible{
      .struct_size = sizeof(arc_canonical_visible_v0),
      .abi_version = ARC_ABI_VERSION,
      .stroke_id = 22,
      .document_revision = 22,
      .target_generation = 8,
      .handoff_token = Token(22),
      .receipt = {.struct_size = sizeof(arc_presentation_receipt_v0),
                  .abi_version = ARC_ABI_VERSION,
                  .evidence = ARC_EVIDENCE_COMPOSITOR_VISIBLE,
                  .status = ARC_STATUS_OK,
                  .target_generation = 8}};
  assert(bridge.CanonicalVisible(visible) == Status::kOk);
  assert(bridge.Find(21) != nullptr);
  assert(bridge.Find(22) == nullptr);
  visible.stroke_id = 21;
  visible.document_revision = 21;
  visible.handoff_token = Token(21);
  assert(bridge.CanonicalVisible(visible) == Status::kOk);
  assert(bridge.StrokeIds().empty());
}

void TestBeginCollisionIsNotSilentlyIdempotent() {
  Bridge bridge(std::make_unique<RecordingBackend>(), arc::CreateNullBackend());
  assert(bridge.Attach(Target()) == Status::kOk);
  auto first = Begin(31);
  assert(bridge.Begin(first) == Status::kOk);
  auto collision = first;
  collision.view_id = 99;
  assert(bridge.Begin(collision) == Status::kInvalidState);
  assert(bridge.diagnostics().begin_collisions == 1);
}

void TestCapacityDegradesPresentationWithoutInputFailure() {
  arc::BridgeLimits limits;
  limits.max_primitives_per_stroke = 1;
  Bridge bridge(std::make_unique<RecordingBackend>(), arc::CreateNullBackend(), limits);
  assert(bridge.Attach(Target()) == Status::kOk);
  assert(bridge.Begin(Begin(32)) == Status::kOk);
  arc_preview_primitive_v0 primitives[2] = {
      {.kind = ARC_PREVIEW_PRIMITIVE_VECTOR_POINT, .radius = 1.0F},
      {.kind = ARC_PREVIEW_PRIMITIVE_VECTOR_POINT, .radius = 2.0F}};
  auto update = Update(32, 1, primitives);
  update.confirmed_append_count = 2;
  assert(bridge.Push(update) == Status::kOk);
  update.preview_revision = 2;
  update.truncate_confirmed_to = 1;
  assert(bridge.Push(update) == Status::kOk);
  assert(bridge.Find(32) != nullptr);
  assert(bridge.Find(32)->preview_revision == 2);
  assert(bridge.Find(32)->preview_suppressed);
  assert(bridge.diagnostics().capacity_degradations == 1);
  assert(bridge.using_fallback());
}

}  // namespace

int main() {
  TestLifecycleAndStaleAck();
  TestPresentationFailureFallsBack();
  TestMultiplePendingStrokesAndGenerationRecovery();
  TestBeginCollisionIsNotSilentlyIdempotent();
  TestCapacityDegradesPresentationWithoutInputFailure();
  return 0;
}
