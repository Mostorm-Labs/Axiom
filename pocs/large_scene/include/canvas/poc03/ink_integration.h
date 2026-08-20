#ifndef CANVAS_POC03_INK_INTEGRATION_H_
#define CANVAS_POC03_INK_INTEGRATION_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "canvas/poc03/large_scene.h"
#include "canvas_poc02/ink_engine.h"

namespace canvas::poc03 {

// Experimental POC-only geometry owner. POC-02 remains authoritative for
// Stroke and AddStrokeOperation; POC-03 stores only stable resource references.
class InkGeometryStore {
 public:
  InkGeometryStore();
  ~InkGeometryStore();
  InkGeometryStore(const InkGeometryStore&) = delete;
  InkGeometryStore& operator=(const InkGeometryStore&) = delete;

  poc02::Status Begin(poc02::StrokeId stroke_id,
                      poc02::PointerId pointer_id,
                      const poc02::BrushDescriptor& brush,
                      const poc02::PointerSampleBatch& first_batch);
  poc02::Status Push(poc02::PointerSampleBatch batch, uint64_t now_us);
  poc02::Status Commit(poc02::AddStrokeOperation* operation);
  poc02::Status Cancel();
  poc02::Status AcknowledgeVisible(poc02::StrokeId id,
                                   uint64_t ink_document_revision);

  [[nodiscard]] const poc02::Stroke* Find(poc02::StrokeId id) const;
  [[nodiscard]] const poc02::DefaultPreviewSink::State* Preview(
      poc02::StrokeId id) const;
  [[nodiscard]] const poc02::StrokeDocument& document() const {
    return document_;
  }
  [[nodiscard]] const poc02::DefaultPreviewSink& preview_sink() const {
    return preview_sink_;
  }
  [[nodiscard]] const poc02::QueueDiagnostics& queue_diagnostics() const;
  [[nodiscard]] std::optional<poc02::StrokeId> active_stroke_id() const {
    return active_stroke_id_;
  }

 private:
  poc02::StrokeDocument document_;
  poc02::DefaultPreviewSink preview_sink_;
  std::unique_ptr<poc02::InputRouter> input_router_;
  std::optional<poc02::StrokeId> active_stroke_id_;
};

class InkSceneAdapter {
 public:
  [[nodiscard]] static Bounds ConservativeBounds(const poc02::Stroke& stroke);
  [[nodiscard]] static NodeRecord Project(const poc02::Stroke& stroke,
                                          uint32_t order,
                                          uint64_t ink_document_revision);
};

struct InkCommitDiagnostics {
  poc02::StrokeId stroke_id = 0;
  uint64_t ink_document_revision = 0;
  uint64_t scene_document_revision = 0;
  CompileDiagnostics compile;
  Bounds world_dirty;
  bool canonical_committed = false;
  bool scene_integrated = false;
  bool visible_acknowledged = false;
};

// Coordinates the canonical handoff. A successful Commit only schedules the
// frame; CompletePresentation is the sole path that emits CanonicalVisible.
class IntegratedInkController {
 public:
  IntegratedInkController(InkGeometryStore& geometry, Document& document,
                          RuntimeScene& scene, TileCache& cache,
                          DeterministicFrameScheduler& scheduler,
                          uint64_t view_id = 1U,
                          uint64_t target_generation = 1U);

  poc02::Status Begin(poc02::StrokeId stroke_id,
                      poc02::PointerId pointer_id,
                      const poc02::BrushDescriptor& brush,
                      const poc02::PointerSampleBatch& first_batch);
  poc02::Status Push(poc02::PointerSampleBatch batch, uint64_t now_us);
  poc02::Status Cancel();
  bool Commit(uint32_t order, InkCommitDiagnostics* diagnostics,
              std::string* error);

  [[nodiscard]] std::optional<FrameInvalidation> BeginFrame();
  bool CompletePresentation(const FrameInvalidation& frame,
                            uint64_t presented_scene_revision,
                            bool presentation_succeeded,
                            InkCommitDiagnostics* diagnostics,
                            std::string* error);
  void ViewChanged(uint64_t target_generation);

  [[nodiscard]] const poc02::DefaultPreviewSink::State* DrawablePreview(
      uint64_t rendered_scene_revision) const;
  [[nodiscard]] bool has_pending_handoff() const {
    return pending_.has_value();
  }

 private:
  struct PendingHandoff {
    poc02::StrokeId stroke_id = 0;
    uint64_t ink_document_revision = 0;
    uint64_t scene_document_revision = 0;
  };

  InkGeometryStore& geometry_;
  Document& document_;
  RuntimeScene& scene_;
  TileCache& cache_;
  DeterministicFrameScheduler& scheduler_;
  SceneCompiler compiler_;
  uint64_t view_id_;
  uint64_t target_generation_;
  std::optional<PendingHandoff> pending_;
};

struct IntegratedScaleConfig {
  uint32_t base_nodes = 1000U;
  uint32_t historical_strokes = 200U;
  uint64_t seed = 0x43414e5641533033ULL;
};

struct IntegratedScaleReport {
  uint32_t base_nodes = 0;
  uint32_t historical_strokes = 0;
  size_t vector_strokes = 0;
  size_t dab_strokes = 0;
  size_t maximum_records_touched = 0;
  size_t full_fallbacks = 0;
  size_t maximum_queue_batches = 0;
  size_t maximum_pending_callbacks = 0;
  std::string ink_document_digest;
  std::string last_stroke_digest;
  std::string document_digest;
  std::string scene_digest;
};

struct IntegratedActionReport {
  size_t maximum_records_touched = 0;
  size_t full_fallbacks = 0;
  size_t maximum_candidates = 0;
  size_t maximum_pending_callbacks = 0;
  uint64_t cache_invalidations = 0;
  uint32_t handoff_frames = 0;
  std::string vector_stroke_digest;
  std::string dab_stroke_digest;
  std::string ink_document_digest;
  std::string document_digest;
  std::string scene_digest;
};

poc02::BrushDescriptor DeterministicBrush(poc02::BrushType type);
poc02::PointerSampleBatch DeterministicInkBatch(uint32_t stroke_index,
                                                uint32_t batch_index,
                                                poc02::StrokeId stroke_id,
                                                uint64_t viewport_revision = 1U);
bool BuildIntegratedScale(const IntegratedScaleConfig& config,
                          Document* document, RuntimeScene* scene,
                          InkGeometryStore* geometry, TileCache* cache,
                          DeterministicFrameScheduler* scheduler,
                          IntegratedScaleReport* report,
                          std::string* error);
bool RunIntegratedActionCycle(uint32_t base_nodes,
                              uint32_t historical_strokes,
                              Document* document, RuntimeScene* scene,
                              InkGeometryStore* geometry, TileCache* cache,
                              DeterministicFrameScheduler* scheduler,
                              IntegratedActionReport* report,
                              std::string* error);

}  // namespace canvas::poc03

#endif  // CANVAS_POC03_INK_INTEGRATION_H_
