#include "arc/arc.hpp"

#include <cassert>
#include <cstdint>
#include <memory>

namespace {

arc_preview_target_v0 Target(uint64_t generation) {
  return {.struct_size = sizeof(arc_preview_target_v0),
          .abi_version = ARC_ABI_VERSION,
          .platform_kind = ARC_PLATFORM_HEADLESS,
          .target_id = 1,
          .target_generation = generation,
          .width_pixels = 800,
          .height_pixels = 600,
          .device_pixel_ratio = 1.0F};
}

arc_preview_begin_v0 Begin(uint64_t id, uint64_t generation) {
  return {.struct_size = sizeof(arc_preview_begin_v0),
          .abi_version = ARC_ABI_VERSION,
          .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
          .coordinate_space = ARC_COORDINATE_SPACE_WORLD,
          .stroke_id = id,
          .view_id = 1,
          .viewport_revision = 1,
          .target_generation = generation,
          .brush = {.struct_size = sizeof(arc_brush_descriptor_v0),
                    .abi_version = ARC_ABI_VERSION,
                    .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
                    .brush_type = ARC_PREVIEW_PRIMITIVE_VECTOR_POINT,
                    .brush_version = 1,
                    .algorithm_version = 1,
                    .size = 4.0F,
                    .opacity = 1.0F}};
}

}  // namespace

int main() {
  for (uint64_t run = 0; run != 100; ++run) {
    arc::Bridge bridge(nullptr, arc::CreateNullBackend());
    assert(bridge.Attach(Target(1)) == arc::Status::kOk);
    assert(bridge.Begin(Begin(run + 1, 1)) == arc::Status::kOk);
    assert(bridge.Begin(Begin(run + 1, 1)) == arc::Status::kOk);
    auto collision = Begin(run + 1, 1);
    collision.view_id = 2;
    assert(bridge.Begin(collision) == arc::Status::kInvalidState);
    bridge.SurfaceLost(1);
    assert(bridge.Attach(Target(2)) == arc::Status::kOk);
    const auto* state = bridge.Find(run + 1);
    assert(state != nullptr && state->target_generation == 2);
    assert(bridge.Cancel({.struct_size = sizeof(arc_preview_cancel_v0),
                          .abi_version = ARC_ABI_VERSION,
                          .stroke_id = run + 1,
                          .target_generation = 2,
                          .reason = 1}) == arc::Status::kOk);
    assert(bridge.Find(run + 1) == nullptr);
  }
  return 0;
}
