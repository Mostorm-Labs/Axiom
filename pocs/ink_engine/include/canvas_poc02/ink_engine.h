#ifndef CANVAS_POC02_INK_ENGINE_H_
#define CANVAS_POC02_INK_ENGINE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace canvas::poc02 {

using StrokeId = uint64_t;
using PointerId = uint64_t;
using ViewId = uint64_t;

enum class Status : uint32_t {
  kOk = 0,
  kInvalidArgument,
  kInvalidState,
  kUnsupportedVersion,
  kSequenceError,
  kInputOverrun,
  kNotFound,
  kStaleGeneration,
  kParseError,
};

std::string_view StatusName(Status status);
std::string NumericConformanceDigest();

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
  bool operator==(const Vec2&) const = default;
};

struct AffineTransform {
  float m00 = 1.0F;
  float m01 = 0.0F;
  float m10 = 0.0F;
  float m11 = 1.0F;
  float tx = 0.0F;
  float ty = 0.0F;
  bool operator==(const AffineTransform&) const = default;
};

enum class PointerTool : uint8_t { kMouse = 1, kPen = 2, kTouch = 3 };
enum class PointerPhase : uint8_t { kDown = 1, kMove = 2, kUp = 3, kHover = 4 };

enum DeviceCapability : uint32_t {
  kCapabilityPressure = 1U << 0U,
  kCapabilityTilt = 1U << 1U,
  kCapabilityContact = 1U << 2U,
  kCapabilityHover = 1U << 3U,
  kCapabilityBarrelButton = 1U << 4U,
  kCapabilityEraserTip = 1U << 5U,
  kCapabilityPalmClassification = 1U << 6U,
};

struct PointerDeviceInfo {
  uint64_t device_id = 0;
  PointerTool tool = PointerTool::kPen;
  uint32_t capabilities = kCapabilityPressure;
  bool barrel_button = false;
  bool eraser_tip = false;
  bool platform_classified_palm = false;
  bool operator==(const PointerDeviceInfo&) const = default;
};

struct PointerSample {
  PointerId pointer_id = 0;
  uint64_t sample_sequence = 0;
  Vec2 position;
  float pressure = 0.5F;
  Vec2 tilt;
  Vec2 contact_size;
  uint64_t timestamp_us = 0;
  PointerPhase phase = PointerPhase::kMove;
};

struct PointerSampleBatch {
  ViewId view_id = 0;
  uint64_t viewport_revision = 0;
  AffineTransform view_to_world;
  PointerDeviceInfo device;
  std::vector<PointerSample> samples;
};

enum class BrushType : uint8_t { kVector = 1, kDab = 2 };

struct BrushDescriptor {
  BrushType type = BrushType::kVector;
  uint32_t brush_version = 1;
  uint32_t algorithm_version = 1;
  float size = 4.0F;
  float spacing = 0.25F;
  float opacity = 1.0F;
  float jitter = 0.0F;
  std::string resource_id;
  std::string resource_content_hash;
  bool operator==(const BrushDescriptor&) const = default;
};

struct CanonicalSample {
  Vec2 position;
  float pressure = 0.5F;
  Vec2 tilt;
  uint64_t timestamp_us = 0;
  bool operator==(const CanonicalSample&) const = default;
};

struct VectorPoint {
  Vec2 position;
  float radius = 1.0F;
  bool operator==(const VectorPoint&) const = default;
};

struct Dab {
  Vec2 position;
  float radius = 1.0F;
  float rotation_degrees = 0.0F;
  float opacity = 1.0F;
  bool operator==(const Dab&) const = default;
};

struct Stroke {
  StrokeId id = 0;
  BrushDescriptor brush;
  std::vector<CanonicalSample> confirmed_samples;
  std::vector<VectorPoint> vector_points;
  std::vector<Dab> dabs;
  bool operator==(const Stroke&) const = default;
};

std::string StrokeDigest(const Stroke& stroke);

struct PreviewPrimitive {
  Vec2 position;
  float radius = 1.0F;
  float rotation_degrees = 0.0F;
  float opacity = 1.0F;
  bool operator==(const PreviewPrimitive&) const = default;
};

struct PreviewStrokeUpdate {
  static constexpr uint32_t kSchemaVersion = 1;
  uint32_t schema_version = kSchemaVersion;
  StrokeId stroke_id = 0;
  uint64_t revision = 0;
  ViewId view_id = 0;
  uint64_t viewport_revision = 0;
  BrushDescriptor brush;
  size_t truncate_confirmed_to = 0;
  std::vector<PreviewPrimitive> confirmed_append;
  std::vector<PreviewPrimitive> predicted_tail;
};

enum class PreviewEventType : uint8_t {
  kBegin,
  kUpdate,
  kCanonicalCommitted,
  kCanonicalVisible,
  kCancel,
};

struct PreviewEvent {
  PreviewEventType type = PreviewEventType::kUpdate;
  StrokeId stroke_id = 0;
  uint64_t revision = 0;
  uint64_t document_revision = 0;
};

class PreviewSink {
 public:
  virtual ~PreviewSink() = default;
  virtual Status Begin(StrokeId id, const BrushDescriptor& brush) = 0;
  virtual Status Push(const PreviewStrokeUpdate& update) = 0;
  virtual Status CanonicalCommitted(StrokeId id, uint64_t document_revision) = 0;
  virtual Status CanonicalVisible(StrokeId id, uint64_t document_revision) = 0;
  virtual Status Cancel(StrokeId id) = 0;
};

class DefaultPreviewSink final : public PreviewSink {
 public:
  struct State {
    BrushDescriptor brush;
    uint64_t revision = 0;
    std::vector<PreviewPrimitive> confirmed;
    std::vector<PreviewPrimitive> predicted;
    bool committed = false;
    bool visible = false;
  };

  Status Begin(StrokeId id, const BrushDescriptor& brush) override;
  Status Push(const PreviewStrokeUpdate& update) override;
  Status CanonicalCommitted(StrokeId id, uint64_t document_revision) override;
  Status CanonicalVisible(StrokeId id, uint64_t document_revision) override;
  Status Cancel(StrokeId id) override;

  [[nodiscard]] const State* Find(StrokeId id) const;
  [[nodiscard]] const std::vector<PreviewEvent>& events() const { return events_; }
  [[nodiscard]] std::string ModelDigest() const;

 private:
  std::map<StrokeId, State> states_;
  std::vector<PreviewEvent> events_;
};

struct PreviewQueueLimits {
  size_t max_updates = 8;
  size_t max_primitives = 8192;
  size_t max_bytes = 1024U * 1024U;
};

struct PreviewQueueDiagnostics {
  size_t updates = 0;
  size_t primitives = 0;
  size_t bytes = 0;
  uint64_t coalesced_updates = 0;
  uint64_t overruns = 0;
};

class PreviewUpdateQueue {
 public:
  explicit PreviewUpdateQueue(PreviewQueueLimits limits = {});
  Status Enqueue(PreviewStrokeUpdate update);
  std::optional<PreviewStrokeUpdate> Pop();
  void Clear();
  [[nodiscard]] const PreviewQueueDiagnostics& diagnostics() const {
    return diagnostics_;
  }

 private:
  void RefreshDiagnostics();
  PreviewQueueLimits limits_;
  PreviewQueueDiagnostics diagnostics_;
  std::deque<PreviewStrokeUpdate> updates_;
};

struct AddStrokeOperation {
  static constexpr uint32_t kSchemaVersion = 1;
  uint32_t schema_version = kSchemaVersion;
  uint64_t sequence = 0;
  Stroke stroke;
};

class StrokeDocument {
 public:
  Status Apply(const AddStrokeOperation& operation);
  [[nodiscard]] const Stroke* Find(StrokeId id) const;
  [[nodiscard]] uint64_t revision() const { return revision_; }
  [[nodiscard]] uint64_t operation_sequence() const { return operation_sequence_; }
  [[nodiscard]] size_t stroke_count() const { return strokes_.size(); }
  [[nodiscard]] size_t indexed_stroke_count() const { return stroke_index_.size(); }
  [[nodiscard]] std::span<const Stroke> strokes() const { return strokes_; }
  [[nodiscard]] size_t EstimatedBytes() const;
  [[nodiscard]] std::string Digest() const;

 private:
  uint64_t revision_ = 0;
  uint64_t operation_sequence_ = 0;
  std::vector<Stroke> strokes_;
  // Derived lookup state only. It is intentionally excluded from serialization
  // and digest semantics so POC-02's reviewed canonical model is unchanged.
  std::unordered_map<StrokeId, size_t> stroke_index_;
};

std::string SerializeAddStrokeNdjson(const AddStrokeOperation& operation);
Status ParseAddStrokeNdjson(std::string_view line, AddStrokeOperation* operation,
                            std::string* error);

class StrokeSession {
 public:
  StrokeSession(StrokeId stroke_id, PointerId pointer_id,
                BrushDescriptor brush, PreviewSink& preview_sink);
  ~StrokeSession();
  StrokeSession(const StrokeSession&) = delete;
  StrokeSession& operator=(const StrokeSession&) = delete;

  Status Begin(const PointerSampleBatch& batch);
  Status Push(const PointerSampleBatch& batch);
  Status End(Stroke* stroke);
  Status Cancel();

  [[nodiscard]] StrokeId stroke_id() const;
  [[nodiscard]] size_t confirmed_input_count() const;
  [[nodiscard]] size_t incremental_work_count() const;
  [[nodiscard]] uint64_t preview_revision() const;
  [[nodiscard]] bool active() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

struct QueueLimits {
  size_t max_batches = 64;
  size_t max_samples = 4096;
  size_t max_bytes = 1024U * 1024U;
  size_t max_batch_samples = 512;
  uint64_t max_oldest_sample_age_us = 250000;
};

struct QueueDiagnostics {
  size_t batches = 0;
  size_t samples = 0;
  size_t bytes = 0;
  uint64_t oldest_sample_age_us = 0;
  uint64_t merged_batches = 0;
  uint64_t overruns = 0;
};

class PointerBatchQueue {
 public:
  explicit PointerBatchQueue(QueueLimits limits = {});
  Status Enqueue(PointerSampleBatch batch, uint64_t now_us);
  std::optional<PointerSampleBatch> Pop(uint64_t now_us);
  void Clear();
  [[nodiscard]] const QueueDiagnostics& diagnostics() const { return diagnostics_; }

 private:
  void RefreshDiagnostics(uint64_t now_us);
  QueueLimits limits_;
  QueueDiagnostics diagnostics_;
  std::deque<PointerSampleBatch> batches_;
};

class InputRouter {
 public:
  InputRouter(StrokeDocument& document, PreviewSink& preview_sink,
              QueueLimits limits = {});
  Status Begin(StrokeId stroke_id, PointerId pointer_id,
               const BrushDescriptor& brush, const PointerSampleBatch& first_batch);
  Status Submit(PointerSampleBatch batch, uint64_t now_us);
  Status Drain(uint64_t now_us);
  Status End(uint64_t operation_sequence, AddStrokeOperation* committed_operation);
  Status Cancel();
  Status AcknowledgeCanonicalVisible(StrokeId stroke_id, uint64_t document_revision);

  [[nodiscard]] const QueueDiagnostics& queue_diagnostics() const;
  [[nodiscard]] const StrokeSession* active_session() const { return session_.get(); }

 private:
  void CancelForOverrun();
  StrokeDocument& document_;
  PreviewSink& preview_sink_;
  PointerBatchQueue queue_;
  std::unique_ptr<StrokeSession> session_;
  std::optional<std::pair<StrokeId, uint64_t>> awaiting_visible_;
};

enum class FrameInvalidationReason : uint32_t {
  kPreview = 1U << 0U,
  kDocument = 1U << 1U,
  kResize = 1U << 2U,
  kRecovery = 1U << 3U,
};

struct FrameInvalidation {
  ViewId view_id = 0;
  uint32_t reasons = 0;
  uint64_t minimum_document_revision = 0;
  uint64_t minimum_preview_revision = 0;
  uint64_t target_generation = 0;
};

struct PresentedFrame {
  ViewId view_id = 0;
  uint64_t document_revision = 0;
  uint64_t preview_revision = 0;
  uint64_t target_generation = 0;
};

class DeterministicFrameScheduler {
 public:
  void Invalidate(const FrameInvalidation& invalidation);
  void SetTargetGeneration(ViewId view_id, uint64_t generation);
  std::optional<FrameInvalidation> BeginFrame(ViewId view_id);
  Status Present(const PresentedFrame& frame);
  [[nodiscard]] size_t pending_callback_count(ViewId view_id) const;
  [[nodiscard]] const PresentedFrame* LastPresented(ViewId view_id) const;

 private:
  std::map<ViewId, FrameInvalidation> pending_;
  std::map<ViewId, uint64_t> generations_;
  std::map<ViewId, PresentedFrame> presented_;
};

struct ReplayFixture {
  StrokeId stroke_id = 0;
  PointerId pointer_id = 0;
  BrushDescriptor brush;
  uint64_t operation_sequence = 1;
  std::vector<PointerSampleBatch> batches;
};

Status ParseReplayFixture(std::string_view ndjson, ReplayFixture* fixture,
                          std::string* error);
Status RunReplayFixture(const ReplayFixture& fixture, StrokeDocument* document,
                        DefaultPreviewSink* sink,
                        AddStrokeOperation* committed_operation,
                        std::string* error);

}  // namespace canvas::poc02

#endif  // CANVAS_POC02_INK_ENGINE_H_
