#include "arc/arc.hpp"
#include "poc02_adapter.hpp"

#include <cassert>
#include <cstdint>
#include <memory>

namespace {

struct FailingPreviewBackend final : arc::PreviewBackend {
  arc_backend_capabilities_v0 Capabilities() const override {
    return {.struct_size = sizeof(arc_backend_capabilities_v0),
            .abi_version = ARC_ABI_VERSION,
            .platform_kind = ARC_PLATFORM_HEADLESS,
            .presentation_capabilities =
                ARC_PRESENTATION_CAPABILITY_INDEPENDENT_TARGET |
                ARC_PRESENTATION_CAPABILITY_REPLACE_TRUNCATE |
                ARC_PRESENTATION_CAPABILITY_PRESENT_RECEIPT,
            .max_pending_strokes = 64,
            .max_primitives_per_update = 1024,
            .max_queue_bytes = 1u << 20u};
  }
  arc::Status Attach(const arc_preview_target_v0&) override { return arc::Status::kOk; }
  arc::Status Detach(uint64_t) override { return arc::Status::kOk; }
  arc::Status Begin(const arc_preview_begin_v0&) override { return arc::Status::kOk; }
  arc::Status Push(const arc_preview_update_v0&) override {
    return arc::Status::kPresentationFailed;
  }
  arc::Status SealInput(const arc_preview_seal_v0&) override { return arc::Status::kOk; }
  arc::Status CanonicalCommitted(const arc_canonical_commit_v0&) override {
    return arc::Status::kOk;
  }
  arc::Status CanonicalVisible(const arc_canonical_visible_v0&) override {
    return arc::Status::kOk;
  }
  arc::Status Cancel(const arc_preview_cancel_v0&) override { return arc::Status::kOk; }
};

}  // namespace

#include "canvas_poc02/ink_engine.h"

int main() {
  arc::Bridge bridge(nullptr, arc::CreateNullBackend());
  const arc_preview_target_v0 target{
      .struct_size = sizeof(arc_preview_target_v0),
      .abi_version = ARC_ABI_VERSION,
      .platform_kind = ARC_PLATFORM_HEADLESS,
      .target_id = 1,
      .target_generation = 1,
      .width_pixels = 800,
      .height_pixels = 600,
      .device_pixel_ratio = 1.0F};
  assert(bridge.Attach(target) == arc::Status::kOk);
  auto sink = canvas::poc06::CreateArcPreviewAdapter(bridge);
  canvas::poc02::StrokeDocument document;
  canvas::poc02::InputRouter router(document, *sink);
  canvas::poc02::BrushDescriptor brush{.type = canvas::poc02::BrushType::kVector,
                                        .size = 4.0F,
                                        .spacing = 0.25F,
                                        .opacity = 1.0F};
  canvas::poc02::PointerSampleBatch down{
      .view_id = 1,
      .viewport_revision = 1,
      .device = {.device_id = 1,
                 .tool = canvas::poc02::PointerTool::kPen,
                 .capabilities = canvas::poc02::kCapabilityPressure},
      .samples = {{.pointer_id = 1,
                   .sample_sequence = 1,
                   .position = {10.0F, 10.0F},
                   .pressure = 0.5F,
                   .timestamp_us = 1000,
                   .phase = canvas::poc02::PointerPhase::kDown}}};
  assert(router.Begin(100, 1, brush, down) == canvas::poc02::Status::kOk);
  canvas::poc02::PointerSampleBatch up = down;
  up.samples = {{.pointer_id = 1,
                 .sample_sequence = 2,
                 .position = {20.0F, 20.0F},
                 .pressure = 0.75F,
                 .timestamp_us = 2000,
                 .phase = canvas::poc02::PointerPhase::kUp}};
  assert(router.Submit(up, 2000) == canvas::poc02::Status::kOk);
  assert(router.Drain(2000) == canvas::poc02::Status::kOk);
  canvas::poc02::AddStrokeOperation operation;
  assert(router.End(1, &operation) == canvas::poc02::Status::kOk);
  const arc::StrokeSnapshot* waiting = bridge.Find(100);
  assert(waiting != nullptr);
  assert(waiting->stage == arc::StrokeStage::kAwaitingCanonical);
  assert(router.AcknowledgeCanonicalVisible(100, document.revision()) ==
         canvas::poc02::Status::kOk);
  assert(bridge.Find(100) == nullptr);
  assert(document.strokes().size() == 1);

  arc::Bridge failing_bridge(std::make_unique<FailingPreviewBackend>(),
                             arc::CreateNullBackend());
  assert(failing_bridge.Attach(target) == arc::Status::kOk);
  auto failing_sink = canvas::poc06::CreateArcPreviewAdapter(failing_bridge);
  canvas::poc02::StrokeDocument failure_document;
  canvas::poc02::InputRouter failure_router(failure_document, *failing_sink);
  assert(failure_router.Begin(101, 1, brush, down) == canvas::poc02::Status::kOk);
  assert(failure_router.Submit(up, 2000) == canvas::poc02::Status::kOk);
  assert(failure_router.Drain(2000) == canvas::poc02::Status::kOk);
  canvas::poc02::AddStrokeOperation failure_operation;
  assert(failure_router.End(1, &failure_operation) == canvas::poc02::Status::kOk);
  assert(failure_router.AcknowledgeCanonicalVisible(
             101, failure_document.revision()) == canvas::poc02::Status::kOk);
  assert(failure_document.strokes().size() == 1);
  assert(failing_bridge.using_fallback());
  assert(failing_bridge.TakeCanonicalRedrawRequest());
  return 0;
}
