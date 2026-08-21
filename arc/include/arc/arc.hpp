#ifndef ARC_ARC_HPP_
#define ARC_ARC_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arc/protocol.h"

namespace arc {

enum class Status : uint32_t {
  kOk = ARC_STATUS_OK,
  kInvalidArgument = ARC_STATUS_INVALID_ARGUMENT,
  kAbiMismatch = ARC_STATUS_ABI_MISMATCH,
  kInvalidState = ARC_STATUS_INVALID_STATE,
  kStaleRevision = ARC_STATUS_STALE_REVISION,
  kNotFound = ARC_STATUS_NOT_FOUND,
  kCapacityExceeded = ARC_STATUS_CAPACITY_EXCEEDED,
  kBackendUnavailable = ARC_STATUS_BACKEND_UNAVAILABLE,
  kPresentationFailed = ARC_STATUS_PRESENTATION_FAILED,
  kSurfaceLost = ARC_STATUS_SURFACE_LOST,
  kInternalError = ARC_STATUS_INTERNAL_ERROR,
};

std::string_view StatusName(Status status);

class PreviewBackend {
 public:
  virtual ~PreviewBackend() = default;
  virtual arc_backend_capabilities_v0 Capabilities() const = 0;
  virtual Status Attach(const arc_preview_target_v0& target) = 0;
  virtual Status Detach(uint64_t target_generation) = 0;
  virtual Status Begin(const arc_preview_begin_v0& begin) = 0;
  virtual Status Push(const arc_preview_update_v0& update) = 0;
  virtual Status SealInput(const arc_preview_seal_v0& seal) = 0;
  virtual Status CanonicalCommitted(const arc_canonical_commit_v0& commit) = 0;
  virtual Status CanonicalVisible(const arc_canonical_visible_v0& visible) = 0;
  virtual Status Cancel(const arc_preview_cancel_v0& cancel) = 0;
};

class PointerSampleSink {
 public:
  virtual ~PointerSampleSink() = default;
  virtual Status Push(const arc_pointer_sample_batch_v0& batch) = 0;
  virtual void SourceLost(uint64_t device_id, Status reason) = 0;
};

class InputSource {
 public:
  virtual ~InputSource() = default;
  virtual arc_backend_capabilities_v0 Capabilities() const = 0;
  virtual Status Start(PointerSampleSink& sink) = 0;
  virtual Status Stop() = 0;
  // Platform hosts submit already-normalized native/coalesced samples here.
  // The source owns no Axiom Document or Stroke state.
  virtual Status SubmitBatch(const arc_pointer_sample_batch_v0& batch) = 0;
  virtual void NotifySourceLost(Status reason) = 0;
};

enum class StrokeStage : uint8_t {
  kActive,
  kSealed,
  kAwaitingCanonical,
  kRecovering,
  kRetired,
  kCancelled,
};

struct StrokeSnapshot {
  uint64_t stroke_id = 0;
  StrokeStage stage = StrokeStage::kActive;
  uint64_t preview_revision = 0;
  uint64_t document_revision = 0;
  uint64_t target_generation = 0;
  arc_handoff_token_v0 handoff_token{};
  bool preview_suppressed = false;
  std::vector<arc_preview_primitive_v0> confirmed;
  std::vector<arc_preview_primitive_v0> predicted;
};

struct Diagnostics {
  uint64_t accepted_updates = 0;
  uint64_t begun_strokes = 0;
  uint64_t sealed_strokes = 0;
  uint64_t canonical_commits = 0;
  uint64_t canonical_visible = 0;
  uint64_t coalesced_or_stale_updates = 0;
  uint64_t backend_failures = 0;
  uint64_t fallback_activations = 0;
  uint64_t stale_acknowledgements = 0;
  uint64_t retired_strokes = 0;
  uint64_t cancelled_strokes = 0;
  uint64_t capacity_degradations = 0;
  uint64_t retired_tombstone_evictions = 0;
  uint64_t begin_collisions = 0;
  Status last_backend_error = Status::kOk;
  std::string last_backend_message;
};

enum class TraceEventType : uint8_t {
  kBegin,
  kUpdate,
  kCanonicalCommitted,
  kCanonicalVisible,
  kCancel,
};

struct TraceEvent {
  TraceEventType type = TraceEventType::kUpdate;
  uint64_t stroke_id = 0;
  uint64_t preview_revision = 0;
  uint64_t document_revision = 0;
};

struct BridgeLimits {
  size_t max_pending_strokes = 64;
  size_t max_primitives_per_stroke = 1u << 20u;
  size_t max_total_bytes = 64u * 1024u * 1024u;
  size_t max_retired_tombstones = 1024;
};

class Bridge {
 public:
  Bridge(std::unique_ptr<PreviewBackend> primary,
         std::unique_ptr<PreviewBackend> fallback,
         BridgeLimits limits = {});
  ~Bridge();
  Bridge(const Bridge&) = delete;
  Bridge& operator=(const Bridge&) = delete;

  Status Attach(const arc_preview_target_v0& target);
  Status Detach(uint64_t target_generation);
  Status Begin(const arc_preview_begin_v0& begin);
  Status Push(const arc_preview_update_v0& update);
  Status SealInput(const arc_preview_seal_v0& seal);
  Status CanonicalCommitted(const arc_canonical_commit_v0& commit);
  Status CanonicalVisible(const arc_canonical_visible_v0& visible);
  Status Cancel(const arc_preview_cancel_v0& cancel);
  void SurfaceLost(uint64_t target_generation);

  [[nodiscard]] bool using_fallback() const;
  [[nodiscard]] bool TakeCanonicalRedrawRequest();
  [[nodiscard]] const Diagnostics& diagnostics() const;
  [[nodiscard]] const std::vector<TraceEvent>& trace() const;
  [[nodiscard]] std::vector<uint64_t> StrokeIds() const;
  [[nodiscard]] const StrokeSnapshot* Find(uint64_t stroke_id) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

std::unique_ptr<PreviewBackend> CreateNullBackend();
std::unique_ptr<PreviewBackend> CreateWebBackend();
std::unique_ptr<InputSource> CreateWebInputSource();
std::unique_ptr<PreviewBackend> CreateWindowsBackend();
std::unique_ptr<InputSource> CreateWindowsInputSource();
std::unique_ptr<PreviewBackend> CreateAndroidBackend();
std::unique_ptr<InputSource> CreateAndroidInputSource();
std::unique_ptr<PreviewBackend> CreateMacOSBackend();
std::unique_ptr<InputSource> CreateMacOSInputSource();
std::unique_ptr<PreviewBackend> CreateIOSBackend();
std::unique_ptr<InputSource> CreateIOSInputSource();
std::unique_ptr<PreviewBackend> CreateIPadOSBackend();
std::unique_ptr<InputSource> CreateIPadOSInputSource();
std::unique_ptr<PreviewBackend> CreateChromiumOSBackend();
std::unique_ptr<InputSource> CreateChromiumOSInputSource();
std::unique_ptr<PreviewBackend> CreateHeadlessBackend();
std::unique_ptr<InputSource> CreateHeadlessInputSource();
std::unique_ptr<PreviewBackend> CreateDeviceBackend();
std::unique_ptr<InputSource> CreateDeviceInputSource();

}  // namespace arc

#endif  // ARC_ARC_HPP_
