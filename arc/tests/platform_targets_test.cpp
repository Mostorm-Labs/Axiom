#include "arc/arc.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>

namespace {

using BackendFactory = std::unique_ptr<arc::PreviewBackend> (*)();
using InputFactory = std::unique_ptr<arc::InputSource> (*)();

struct PlatformCase {
  uint32_t kind;
  BackendFactory backend;
  InputFactory input;
  bool has_display;
};

struct RecordingSink final : arc::PointerSampleSink {
  arc::Status Push(const arc_pointer_sample_batch_v0& batch) override {
    ++batches;
    last_device = batch.device_id;
    return arc::Status::kOk;
  }
  void SourceLost(uint64_t device_id, arc::Status reason) override {
    lost_device = device_id;
    lost_reason = reason;
  }

  uint32_t batches = 0;
  uint64_t last_device = 0;
  uint64_t lost_device = 0;
  arc::Status lost_reason = arc::Status::kOk;
};

}  // namespace

int main() {
  const std::array platforms{
      PlatformCase{ARC_PLATFORM_WEB, arc::CreateWebBackend,
                   arc::CreateWebInputSource, true},
      PlatformCase{ARC_PLATFORM_WINDOWS, arc::CreateWindowsBackend,
                   arc::CreateWindowsInputSource, true},
      PlatformCase{ARC_PLATFORM_ANDROID, arc::CreateAndroidBackend,
                   arc::CreateAndroidInputSource, true},
      PlatformCase{ARC_PLATFORM_MACOS, arc::CreateMacOSBackend,
                   arc::CreateMacOSInputSource, true},
      PlatformCase{ARC_PLATFORM_IOS, arc::CreateIOSBackend,
                   arc::CreateIOSInputSource, true},
      PlatformCase{ARC_PLATFORM_IPADOS, arc::CreateIPadOSBackend,
                   arc::CreateIPadOSInputSource, true},
      PlatformCase{ARC_PLATFORM_CHROMIUMOS, arc::CreateChromiumOSBackend,
                   arc::CreateChromiumOSInputSource, true},
      PlatformCase{ARC_PLATFORM_HEADLESS, arc::CreateHeadlessBackend,
                   arc::CreateHeadlessInputSource, false},
      PlatformCase{ARC_PLATFORM_DEVICE_DIRECT_PLANE, arc::CreateDeviceBackend,
                   arc::CreateDeviceInputSource, true},
  };
  for (const auto& platform : platforms) {
    auto backend = platform.backend();
    auto input = platform.input();
    assert(backend != nullptr && input != nullptr);
    const auto presentation = backend->Capabilities();
    const auto input_capabilities = input->Capabilities();
    assert(presentation.struct_size == sizeof(arc_backend_capabilities_v0));
    assert(presentation.abi_version == ARC_ABI_VERSION);
    assert(presentation.platform_kind == platform.kind);
    assert(input_capabilities.platform_kind == platform.kind);
    if (platform.has_display) {
      assert((presentation.presentation_capabilities &
              ARC_PRESENTATION_CAPABILITY_INDEPENDENT_TARGET) != 0);
    }
    const arc_preview_target_v0 target{
        .struct_size = sizeof(arc_preview_target_v0),
        .abi_version = ARC_ABI_VERSION,
        .platform_kind = platform.kind,
        .target_id = 100 + platform.kind,
        .target_generation = 1,
        .width_pixels = 800,
        .height_pixels = 600,
        .device_pixel_ratio = 1.0F,
        .opaque_platform_handle = platform.has_display ? 0x1u : 0u};
    assert(backend->Attach(target) == arc::Status::kOk);
    assert(backend->Detach(1) == arc::Status::kOk);
    assert(backend->Detach(1) == arc::Status::kStaleRevision);

    RecordingSink sink;
    assert(input->Start(sink) == arc::Status::kOk);
    arc_pointer_sample_v0 sample{.pointer_id = 9,
                                 .sample_sequence = 1,
                                 .timestamp_us = 1000,
                                 .x = 10.0F,
                                 .y = 20.0F,
                                 .pressure = 0.5F,
                                 .phase = ARC_POINTER_PHASE_DOWN,
                                 .provenance = ARC_SAMPLE_CONFIRMED_CURRENT};
    const arc_pointer_sample_batch_v0 batch{
        .struct_size = sizeof(arc_pointer_sample_batch_v0),
        .abi_version = ARC_ABI_VERSION,
        .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
        .coordinate_space = ARC_COORDINATE_SPACE_VIEW_LOGICAL,
        .view_id = 1,
        .viewport_revision = 1,
        .device_id = 77,
        .input_capabilities = input_capabilities.input_capabilities,
        .tool = ARC_INPUT_TOOL_PEN,
        .samples = &sample,
        .sample_count = 1,
        .sample_stride = sizeof(arc_pointer_sample_v0)};
    assert(input->SubmitBatch(batch) == arc::Status::kOk);
    assert(sink.batches == 1 && sink.last_device == 77);
    input->NotifySourceLost(arc::Status::kSurfaceLost);
    assert(sink.lost_device == 77);
    assert(sink.lost_reason == arc::Status::kSurfaceLost);
    assert(input->Stop() == arc::Status::kOk);
  }
  return 0;
}
