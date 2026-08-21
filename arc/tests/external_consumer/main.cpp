#include "arc/host_adapter.hpp"

int main() {
  arc::HostAdapter host(arc::CreateHeadlessBackend(), arc::CreateNullBackend(),
                        arc::CreateHeadlessInputSource());
  const arc_preview_target_v0 target{
      .struct_size = sizeof(arc_preview_target_v0),
      .abi_version = ARC_ABI_VERSION,
      .platform_kind = ARC_PLATFORM_HEADLESS,
      .target_id = 1,
      .target_generation = 1,
      .width_pixels = 1,
      .height_pixels = 1,
      .device_pixel_ratio = 1.0F};
  return host.AttachTarget(target) == arc::Status::kOk &&
                 host.target_generation() == 1
             ? 0
             : 1;
}
