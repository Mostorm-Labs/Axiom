#ifndef ARC_PLATFORM_KIND
#error "ARC_PLATFORM_KIND must be defined before including backend_factory.inc"
#endif
#ifndef ARC_CREATE_BACKEND
#error "ARC_CREATE_BACKEND must be defined before including backend_factory.inc"
#endif
#ifndef ARC_CREATE_INPUT_SOURCE
#error "ARC_CREATE_INPUT_SOURCE must be defined before including backend_factory.inc"
#endif
#ifndef ARC_REQUIRES_PLATFORM_HANDLE
#define ARC_REQUIRES_PLATFORM_HANDLE 0
#endif
#ifndef ARC_INPUT_CAPABILITIES
#define ARC_INPUT_CAPABILITIES 0
#endif
#ifndef ARC_PRESENTATION_CAPABILITIES
#define ARC_PRESENTATION_CAPABILITIES \
  (ARC_PRESENTATION_CAPABILITY_INDEPENDENT_TARGET | \
   ARC_PRESENTATION_CAPABILITY_REPLACE_TRUNCATE | \
   ARC_PRESENTATION_CAPABILITY_PRESENT_RECEIPT)
#endif

#include "arc/arc.hpp"

#include <cstdint>
#include <limits>
#include <memory>

namespace arc {
namespace {

class PlatformPreviewBackend final : public PreviewBackend {
 public:
  arc_backend_capabilities_v0 Capabilities() const override {
    return {.struct_size = sizeof(arc_backend_capabilities_v0),
            .abi_version = ARC_ABI_VERSION,
            .platform_kind = ARC_PLATFORM_KIND,
            .presentation_capabilities = ARC_PRESENTATION_CAPABILITIES,
            .max_pending_strokes = 64,
            .max_primitives_per_update = 1u << 20u,
            .max_queue_bytes = 64u * 1024u * 1024u};
  }

  Status Attach(const arc_preview_target_v0& target) override {
    if (target.platform_kind != ARC_PLATFORM_KIND ||
        target.target_generation == 0 ||
        (ARC_REQUIRES_PLATFORM_HANDLE && target.opaque_platform_handle == 0)) {
      return Status::kInvalidArgument;
    }
    generation_ = target.target_generation;
    attached_ = true;
    return Status::kOk;
  }
  Status Detach(uint64_t generation) override {
    if (!attached_ || generation != generation_) return Status::kStaleRevision;
    attached_ = false;
    return Status::kOk;
  }
  Status Begin(const arc_preview_begin_v0& begin) override {
    return attached_ && begin.target_generation == generation_ ? Status::kOk
                                                   : Status::kStaleRevision;
  }
  Status Push(const arc_preview_update_v0& update) override {
    return attached_ && update.target_generation == generation_ ? Status::kOk
                                                    : Status::kStaleRevision;
  }
  Status SealInput(const arc_preview_seal_v0& seal) override {
    return attached_ && seal.target_generation == generation_ ? Status::kOk
                                                  : Status::kStaleRevision;
  }
  Status CanonicalCommitted(const arc_canonical_commit_v0& commit) override {
    return attached_ && commit.target_generation == generation_ ? Status::kOk
                                                    : Status::kStaleRevision;
  }
  Status CanonicalVisible(const arc_canonical_visible_v0& visible) override {
    return attached_ && visible.target_generation == generation_ ? Status::kOk
                                                      : Status::kStaleRevision;
  }
  Status Cancel(const arc_preview_cancel_v0& cancel) override {
    return attached_ && cancel.target_generation == generation_ ? Status::kOk
                                                    : Status::kStaleRevision;
  }

 private:
  uint64_t generation_ = 0;
  bool attached_ = false;
};

class PlatformInputSource final : public InputSource {
 public:
  arc_backend_capabilities_v0 Capabilities() const override {
    return {.struct_size = sizeof(arc_backend_capabilities_v0),
            .abi_version = ARC_ABI_VERSION,
            .platform_kind = ARC_PLATFORM_KIND,
            .input_capabilities = ARC_INPUT_CAPABILITIES,
            .max_pending_strokes = 64,
            .max_primitives_per_update = 1u << 20u,
            .max_queue_bytes = 64u * 1024u * 1024u};
  }

  Status Start(PointerSampleSink& sink) override {
    if (sink_ != nullptr) return Status::kInvalidState;
    sink_ = &sink;
    return Status::kOk;
  }

  Status Stop() override {
    sink_ = nullptr;
    return Status::kOk;
  }

  Status SubmitBatch(const arc_pointer_sample_batch_v0& batch) override {
    if (sink_ == nullptr) return Status::kInvalidState;
    if (batch.struct_size < sizeof(arc_pointer_sample_batch_v0) ||
        batch.abi_version != ARC_ABI_VERSION ||
        batch.schema_version != ARC_PROTOCOL_SCHEMA_VERSION ||
        batch.view_id == 0 || batch.device_id == 0 || batch.samples == nullptr ||
        batch.sample_count == 0 ||
        batch.sample_stride < sizeof(arc_pointer_sample_v0)) {
      return Status::kInvalidArgument;
    }
    device_id_ = batch.device_id;
    return sink_->Push(batch);
  }

  void NotifySourceLost(Status reason) override {
    if (sink_ != nullptr) sink_->SourceLost(device_id_, reason);
  }

 private:
  PointerSampleSink* sink_ = nullptr;
  uint64_t device_id_ = 0;
};

}  // namespace

std::unique_ptr<PreviewBackend> ARC_CREATE_BACKEND() {
  return std::make_unique<PlatformPreviewBackend>();
}

std::unique_ptr<InputSource> ARC_CREATE_INPUT_SOURCE() {
  return std::make_unique<PlatformInputSource>();
}

}  // namespace arc
