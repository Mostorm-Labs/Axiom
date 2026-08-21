#include "canvas/poc03/ink_integration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace canvas::poc03 {
namespace {

constexpr poc02::StrokeId kStrokeIdBase = UINT64_C(0x1000000000000000);

uint32_t StrokeColor(const poc02::Stroke& stroke) {
  return stroke.brush.type == poc02::BrushType::kVector ? 0xff1f5bd2U
                                                        : 0xff7437bdU;
}

void IncludeCircle(const poc02::Vec2& position, float radius,
                   bool* initialized, Bounds* bounds) {
  const Bounds circle{position.x - radius, position.y - radius,
                      position.x + radius, position.y + radius};
  *bounds = *initialized ? Bounds::Union(*bounds, circle) : circle;
  *initialized = true;
}

bool SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
  return false;
}

NodeRecord MakeBaseNode(uint32_t index, uint32_t base_nodes, uint64_t seed) {
  const uint32_t column = index % 1000U;
  const uint32_t row = index / 1000U;
  const uint64_t mixed = seed + static_cast<uint64_t>(index) *
      UINT64_C(0x9e3779b97f4a7c15);
  const float left = static_cast<float>(column * 32U + (mixed & 7U));
  const float top = static_cast<float>(row * 32U + ((mixed >> 8U) & 7U));
  const float width = 12.0F + static_cast<float>((mixed >> 16U) & 15U);
  const float height = 12.0F + static_cast<float>((mixed >> 24U) & 15U);
  const NodeType type = static_cast<NodeType>((index % 4U) + 1U);
  return NodeRecord{
      .id = static_cast<uint64_t>(index) + 1U,
      .order = index,
      .type = type,
      .bounds = {left, top, left + width, top + height},
      .rgba = 0xff000000U | static_cast<uint32_t>((mixed >> 32U) & 0xffffffU),
      .resource_key = type == NodeType::kImage || type == NodeType::kSimpleText
                          ? 1U + (index % 17U)
                          : 0U,
      .content_revision = 1U,
      .locked = index >= base_nodes,
  };
}

}  // namespace

InkGeometryStore::InkGeometryStore()
    : input_router_(std::make_unique<poc02::InputRouter>(document_,
                                                         preview_sink_)) {}

InkGeometryStore::~InkGeometryStore() = default;

poc02::Status InkGeometryStore::Begin(
    poc02::StrokeId stroke_id, poc02::PointerId pointer_id,
    const poc02::BrushDescriptor& brush,
    const poc02::PointerSampleBatch& first_batch) {
  const poc02::Status status = input_router_->Begin(
      stroke_id, pointer_id, brush, first_batch);
  if (status == poc02::Status::kOk) {
    active_stroke_id_ = stroke_id;
  }
  return status;
}

poc02::Status InkGeometryStore::Push(poc02::PointerSampleBatch batch,
                                     uint64_t now_us) {
  poc02::Status status = input_router_->Submit(std::move(batch), now_us);
  if (status != poc02::Status::kOk) {
    active_stroke_id_.reset();
    return status;
  }
  status = input_router_->Drain(now_us);
  if (status != poc02::Status::kOk) {
    active_stroke_id_.reset();
  }
  return status;
}

poc02::Status InkGeometryStore::Commit(
    poc02::AddStrokeOperation* operation) {
  const poc02::Status status = input_router_->End(
      document_.operation_sequence() + 1U, operation);
  active_stroke_id_.reset();
  return status;
}

poc02::Status InkGeometryStore::Cancel() {
  const poc02::Status status = input_router_->Cancel();
  active_stroke_id_.reset();
  return status;
}

poc02::Status InkGeometryStore::AcknowledgeVisible(
    poc02::StrokeId id, uint64_t ink_document_revision) {
  return input_router_->AcknowledgeCanonicalVisible(id,
                                                     ink_document_revision);
}

const poc02::Stroke* InkGeometryStore::Find(poc02::StrokeId id) const {
  return document_.Find(id);
}

const poc02::DefaultPreviewSink::State* InkGeometryStore::Preview(
    poc02::StrokeId id) const {
  return preview_sink_.Find(id);
}

const poc02::QueueDiagnostics& InkGeometryStore::queue_diagnostics() const {
  return input_router_->queue_diagnostics();
}

Bounds InkSceneAdapter::ConservativeBounds(const poc02::Stroke& stroke) {
  bool initialized = false;
  Bounds result;
  for (const poc02::VectorPoint& point : stroke.vector_points) {
    IncludeCircle(point.position, point.radius, &initialized, &result);
  }
  for (const poc02::Dab& dab : stroke.dabs) {
    // A rotated Dab is an ellipse whose major axis is radius. Its enclosing
    // circle is deterministic and conservative for every rotation.
    IncludeCircle(dab.position, dab.radius, &initialized, &result);
  }
  if (!initialized || !result.IsFiniteAndOrdered()) {
    throw std::invalid_argument("canonical Stroke has no finite geometry");
  }
  return result;
}

NodeRecord InkSceneAdapter::Project(const poc02::Stroke& stroke,
                                    uint32_t order,
                                    uint64_t ink_document_revision) {
  return NodeRecord{
      .id = stroke.id,
      .order = order,
      .type = NodeType::kStroke,
      .bounds = ConservativeBounds(stroke),
      .rgba = StrokeColor(stroke),
      .resource_key = stroke.id,
      .content_revision = ink_document_revision,
      .locked = false,
  };
}

IntegratedInkController::IntegratedInkController(
    InkGeometryStore& geometry, Document& document, RuntimeScene& scene,
    TileCache& cache, DeterministicFrameScheduler& scheduler,
    uint64_t view_id, uint64_t target_generation)
    : geometry_(geometry), document_(document), scene_(scene), cache_(cache),
      scheduler_(scheduler), view_id_(view_id),
      target_generation_(target_generation) {}

poc02::Status IntegratedInkController::Begin(
    poc02::StrokeId stroke_id, poc02::PointerId pointer_id,
    const poc02::BrushDescriptor& brush,
    const poc02::PointerSampleBatch& first_batch) {
  if (pending_) {
    return poc02::Status::kInvalidState;
  }
  return geometry_.Begin(stroke_id, pointer_id, brush, first_batch);
}

poc02::Status IntegratedInkController::Push(
    poc02::PointerSampleBatch batch, uint64_t now_us) {
  return geometry_.Push(std::move(batch), now_us);
}

poc02::Status IntegratedInkController::Cancel() {
  return geometry_.Cancel();
}

bool IntegratedInkController::Commit(uint32_t order,
                                     InkCommitDiagnostics* diagnostics,
                                     std::string* error) {
  if (diagnostics == nullptr || pending_) {
    return SetError(error, "commit diagnostics required and handoff must be idle");
  }
  *diagnostics = {};
  poc02::AddStrokeOperation ink_operation;
  const poc02::Status ink_status = geometry_.Commit(&ink_operation);
  if (ink_status != poc02::Status::kOk) {
    return SetError(error, "POC-02 canonical commit failed: " +
                               std::string(poc02::StatusName(ink_status)));
  }
  diagnostics->stroke_id = ink_operation.stroke.id;
  diagnostics->ink_document_revision = geometry_.document().revision();
  diagnostics->canonical_committed = true;

  NodeRecord node;
  try {
    node = InkSceneAdapter::Project(ink_operation.stroke, order,
                                    geometry_.document().revision());
  } catch (const std::exception& exception) {
    return SetError(error, exception.what());
  }
  ChangeSet changes;
  if (!document_.Apply(
          Operation{OperationKind::kCreate, node.id, node}, &changes, error)) {
    return false;
  }
  bool compiled = false;
  try {
    compiled = compiler_.ApplyIncremental(document_, changes, &scene_,
                                           &diagnostics->compile, error);
    if (!compiled) {
      ++diagnostics->compile.full_fallbacks;
      scene_ = compiler_.CompileFull(document_, &diagnostics->compile);
      compiled = true;
    }
  } catch (const std::exception& incremental_error) {
    try {
      ++diagnostics->compile.full_fallbacks;
      scene_ = compiler_.CompileFull(document_, &diagnostics->compile);
      compiled = true;
      if (error != nullptr) {
        *error = std::string("incremental exception recovered by full compile: ") +
                 incremental_error.what();
      }
    } catch (const std::exception& full_error) {
      return SetError(error, std::string("incremental and full Scene compile failed: ") +
                                 full_error.what());
    }
  }
  if (!compiled || scene_.source_revision() != document_.revision()) {
    return SetError(error, "canonical Stroke retained but Scene integration failed");
  }

  diagnostics->scene_document_revision = document_.revision();
  diagnostics->world_dirty = node.bounds;
  diagnostics->scene_integrated = true;
  cache_.InvalidateWorld(view_id_, node.bounds, 256.0F);
  scheduler_.Invalidate(FrameInvalidation{
      .view_id = view_id_,
      .minimum_document_revision = document_.revision(),
      .minimum_view_revision = 0U,
      .minimum_preview_revision = geometry_.Preview(node.id) != nullptr
                                      ? geometry_.Preview(node.id)->revision
                                      : 0U,
      .target_generation = target_generation_,
      .reason_mask = static_cast<uint32_t>(InvalidationReason::kDocument) |
                     static_cast<uint32_t>(InvalidationReason::kPreview),
  });
  pending_ = PendingHandoff{node.id, geometry_.document().revision(),
                            document_.revision()};
  if (error != nullptr && diagnostics->compile.full_fallbacks == 0U) {
    error->clear();
  }
  return true;
}

std::optional<FrameInvalidation> IntegratedInkController::BeginFrame() {
  return scheduler_.Pump(view_id_, target_generation_);
}

bool IntegratedInkController::CompletePresentation(
    const FrameInvalidation& frame, uint64_t presented_scene_revision,
    bool presentation_succeeded, InkCommitDiagnostics* diagnostics,
    std::string* error) {
  if (!presentation_succeeded) {
    return SetError(error, "presentation failed; canonical Stroke remains pending");
  }
  if (presented_scene_revision < frame.minimum_document_revision ||
      !scheduler_.Present(frame, target_generation_)) {
    return SetError(error, "stale frame cannot acknowledge canonical visibility");
  }
  if (pending_ && presented_scene_revision >= pending_->scene_document_revision) {
    const poc02::Status status = geometry_.AcknowledgeVisible(
        pending_->stroke_id, pending_->ink_document_revision);
    if (status != poc02::Status::kOk) {
      return SetError(error, "POC-02 visible acknowledgement failed: " +
                                 std::string(poc02::StatusName(status)));
    }
    if (diagnostics != nullptr) {
      diagnostics->visible_acknowledged = true;
    }
    pending_.reset();
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void IntegratedInkController::ViewChanged(uint64_t target_generation) {
  if (geometry_.active_stroke_id()) {
    geometry_.Cancel();
  }
  target_generation_ = target_generation;
  scheduler_.DestroyView(view_id_);
  if (pending_) {
    // A committed Stroke is never rolled back. Request it on the replacement
    // target and defer the visibility acknowledgement to that presentation.
    scheduler_.Invalidate(FrameInvalidation{
        .view_id = view_id_,
        .minimum_document_revision = pending_->scene_document_revision,
        .target_generation = target_generation_,
        .reason_mask = static_cast<uint32_t>(InvalidationReason::kSurface),
    });
  }
}

const poc02::DefaultPreviewSink::State*
IntegratedInkController::DrawablePreview(
    uint64_t rendered_scene_revision) const {
  const std::optional<poc02::StrokeId> active = geometry_.active_stroke_id();
  if (active) {
    return geometry_.Preview(*active);
  }
  if (pending_ && rendered_scene_revision < pending_->scene_document_revision) {
    return geometry_.Preview(pending_->stroke_id);
  }
  return nullptr;
}

poc02::BrushDescriptor DeterministicBrush(poc02::BrushType type) {
  if (type == poc02::BrushType::kVector) {
    return poc02::BrushDescriptor{
        .type = type,
        .brush_version = 1U,
        .algorithm_version = 1U,
        .size = 8.0F,
        .spacing = 0.25F,
        .opacity = 0.9F,
        .jitter = 0.0F,
        .resource_id = {},
        .resource_content_hash = {},
    };
  }
  return poc02::BrushDescriptor{
      .type = type,
      .size = 10.0F,
      .spacing = 0.4F,
      .opacity = 0.8F,
      .jitter = 0.2F,
      .resource_id = "fixture/checker",
      .resource_content_hash = "d9f00b",
  };
}

poc02::PointerSampleBatch DeterministicInkBatch(
    uint32_t stroke_index, uint32_t batch_index,
    poc02::StrokeId stroke_id, uint64_t viewport_revision) {
  if (batch_index >= 4U) {
    throw std::invalid_argument("integrated Ink batch index must be 0..3");
  }
  constexpr std::array<float, 4> pressure{0.2F, 0.4F, 0.6F, 0.8F};
  const float origin_x = static_cast<float>((stroke_index % 1000U) * 32U + 4U);
  const float origin_y = static_cast<float>(((stroke_index / 1000U) % 100U) *
                                            32U + 4U);
  poc02::PointerSampleBatch batch{
      .view_id = 1U,
      .viewport_revision = viewport_revision,
      .view_to_world = {},
      .device = {
          .device_id = 5U,
          .tool = poc02::PointerTool::kPen,
          .capabilities = poc02::kCapabilityPressure | poc02::kCapabilityTilt,
      },
      .samples = {},
  };
  batch.samples.reserve(4U);
  for (uint32_t item = 0U; item < 4U; ++item) {
    const uint32_t sample_index = batch_index * 4U + item;
    const uint64_t timestamp = static_cast<uint64_t>(stroke_index) * 100000U +
                               static_cast<uint64_t>(sample_index) * 4000U;
    batch.samples.push_back(poc02::PointerSample{
        .pointer_id = stroke_id,
        .sample_sequence = sample_index,
        .position = {origin_x + static_cast<float>(sample_index * 2U),
                     origin_y + static_cast<float>(
                         (sample_index * sample_index + stroke_index) % 7U)},
        .pressure = pressure[sample_index % pressure.size()],
        .tilt = {0.1F, -0.2F},
        .contact_size = {2.0F, 2.0F},
        .timestamp_us = timestamp,
        .phase = sample_index == 0U
                     ? poc02::PointerPhase::kDown
                     : (sample_index == 15U ? poc02::PointerPhase::kUp
                                            : poc02::PointerPhase::kMove),
    });
  }
  return batch;
}

bool BuildIntegratedScale(
    const IntegratedScaleConfig& config, Document* document,
    RuntimeScene* scene, InkGeometryStore* geometry, TileCache* cache,
    DeterministicFrameScheduler* scheduler, IntegratedScaleReport* report,
    std::string* error) {
  if (document == nullptr || scene == nullptr || geometry == nullptr ||
      cache == nullptr || scheduler == nullptr || report == nullptr ||
      config.base_nodes == 0U || config.historical_strokes == 0U ||
      config.base_nodes > std::numeric_limits<uint32_t>::max() -
                              config.historical_strokes) {
    return SetError(error, "invalid integrated scale arguments");
  }
  *report = {};
  try {
    for (uint32_t index = 0; index < config.base_nodes; ++index) {
      const NodeRecord node = MakeBaseNode(index, config.base_nodes,
                                           config.seed);
      ChangeSet ignored;
      if (!document->Apply(
              Operation{OperationKind::kCreate, node.id, node}, &ignored,
              error)) {
        return false;
      }
    }
    *scene = SceneCompiler().CompileFull(*document);
    IntegratedInkController controller(*geometry, *document, *scene, *cache,
                                       *scheduler);
    for (uint32_t index = 0; index < config.historical_strokes; ++index) {
      const poc02::StrokeId id = kStrokeIdBase + index + 1U;
      const poc02::BrushType type = (index & 1U) == 0U
                                        ? poc02::BrushType::kVector
                                        : poc02::BrushType::kDab;
      poc02::Status status = controller.Begin(
          id, id, DeterministicBrush(type),
          DeterministicInkBatch(index, 0U, id));
      if (status != poc02::Status::kOk) {
        return SetError(error, "historical Begin failed: " +
                                   std::string(poc02::StatusName(status)));
      }
      for (uint32_t batch = 1U; batch < 4U; ++batch) {
        poc02::PointerSampleBatch input = DeterministicInkBatch(index, batch, id);
        const uint64_t now_us = input.samples.back().timestamp_us;
        status = controller.Push(std::move(input), now_us);
        if (status != poc02::Status::kOk) {
          return SetError(error, "historical Push failed: " +
                                     std::string(poc02::StatusName(status)));
        }
      }
      InkCommitDiagnostics diagnostics;
      if (!controller.Commit(config.base_nodes + index, &diagnostics, error)) {
        return false;
      }
      report->maximum_records_touched = std::max(
          report->maximum_records_touched,
          diagnostics.compile.records_touched);
      report->full_fallbacks += diagnostics.compile.full_fallbacks;
      report->maximum_queue_batches = std::max(
          report->maximum_queue_batches,
          geometry->queue_diagnostics().batches);
      report->maximum_pending_callbacks = std::max(
          report->maximum_pending_callbacks,
          scheduler->pending_callback_count());
      const auto frame = controller.BeginFrame();
      if (!frame || !controller.CompletePresentation(
                        *frame, scene->source_revision(), true, &diagnostics,
                        error) || !diagnostics.visible_acknowledged) {
        return SetError(error, "historical canonical handoff was not acknowledged");
      }
      if (type == poc02::BrushType::kVector) {
        ++report->vector_strokes;
      } else {
        ++report->dab_strokes;
      }
      report->last_stroke_digest = poc02::StrokeDigest(*geometry->Find(id));
    }
  } catch (const std::exception& exception) {
    return SetError(error, exception.what());
  }
  report->base_nodes = config.base_nodes;
  report->historical_strokes = config.historical_strokes;
  report->ink_document_digest = geometry->document().Digest();
  report->document_digest = document->Digest();
  report->scene_digest = scene->Digest();
  if (scene->Digest() != SceneCompiler().CompileFull(*document).Digest()) {
    return SetError(error, "integrated incremental/full Scene digest differs");
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool RunIntegratedActionCycle(
    uint32_t base_nodes, uint32_t historical_strokes, Document* document,
    RuntimeScene* scene, InkGeometryStore* geometry, TileCache* cache,
    DeterministicFrameScheduler* scheduler, IntegratedActionReport* report,
    std::string* error) {
  if (document == nullptr || scene == nullptr || geometry == nullptr ||
      cache == nullptr || scheduler == nullptr || report == nullptr ||
      base_nodes == 0U) {
    return SetError(error, "invalid integrated action-cycle arguments");
  }
  *report = {};
  IntegratedInkController controller(*geometry, *document, *scene, *cache,
                                     *scheduler);
  constexpr std::array<poc02::StrokeId, 2> ids{
      UINT64_C(0x7000000000000001), UINT64_C(0x7000000000000002)};
  constexpr std::array<poc02::BrushType, 2> types{
      poc02::BrushType::kVector, poc02::BrushType::kDab};
  for (uint32_t stroke_index = 0U; stroke_index < ids.size(); ++stroke_index) {
    const uint32_t trace_index = base_nodes + stroke_index + 1U;
    poc02::Status status = controller.Begin(
        ids[stroke_index], ids[stroke_index],
        DeterministicBrush(types[stroke_index]),
        DeterministicInkBatch(trace_index, 0U, ids[stroke_index]));
    if (status != poc02::Status::kOk ||
        controller.DrawablePreview(scene->source_revision()) == nullptr) {
      return SetError(error, "action-cycle active Preview begin failed");
    }
    for (uint32_t batch_index = 1U; batch_index < 4U; ++batch_index) {
      auto batch = DeterministicInkBatch(trace_index, batch_index,
                                         ids[stroke_index]);
      const uint64_t now_us = batch.samples.back().timestamp_us;
      status = controller.Push(std::move(batch), now_us);
      if (status != poc02::Status::kOk) {
        return SetError(error, "action-cycle input batch failed: " +
                                   std::string(poc02::StatusName(status)));
      }
    }
    const auto* preview = controller.DrawablePreview(scene->source_revision());
    if (preview == nullptr || preview->confirmed.empty()) {
      return SetError(error, "action-cycle Preview geometry unavailable");
    }
    const poc02::Vec2 cache_probe = preview->confirmed.front().position;
    cache->Put(TileKey{
                   .view_id = 1U,
                   .content_revision = scene->source_revision(),
                   .device_generation = cache->device_generation(),
                   .backend_capability = 1U,
                   .scale_bucket = 1U,
                   .color_space = 1U,
                   .tile_x = static_cast<int32_t>(
                       std::floor(cache_probe.x / 256.0F)),
                   .tile_y = static_cast<int32_t>(
                       std::floor(cache_probe.y / 256.0F)),
               },
               256U * 256U * 4U);
    const uint64_t prior_scene_revision = scene->source_revision();
    InkCommitDiagnostics diagnostics;
    if (!controller.Commit(base_nodes + historical_strokes + stroke_index,
                           &diagnostics, error)) {
      return false;
    }
    if (controller.DrawablePreview(prior_scene_revision) == nullptr ||
        controller.DrawablePreview(scene->source_revision()) != nullptr) {
      return SetError(error, "action-cycle Preview/Canonical overlap failed");
    }
    const ViewState view{
        1U, 1U, 1U, Bounds{0.0F, 0.0F, 1920.0F, 1080.0F},
        1.0F, 1.0F, 1920U, 1080U};
    const ViewQueryResult query = QueryView(*scene, view,
                                            diagnostics.world_dirty);
    report->maximum_candidates = std::max(report->maximum_candidates,
                                           query.candidates.size());
    const FrameGraph graph = BuildFrame(
        *scene, query,
        OverlayState{
            .editor_ids = {},
            .presence_ids = {},
            .preview_ids = {ids[stroke_index]},
            .selection_ids = {},
            .hud_ids = {},
        });
    if (graph.logical_passes[static_cast<size_t>(LogicalPass::kInk)]
            .item_ids.empty()) {
      return SetError(error, "action-cycle Ink pass is empty");
    }
    const auto frame = controller.BeginFrame();
    if (!frame || !controller.CompletePresentation(
                      *frame, scene->source_revision(), true, &diagnostics,
                      error) || !diagnostics.visible_acknowledged) {
      return SetError(error, "action-cycle visible handoff failed");
    }
    report->maximum_records_touched = std::max(
        report->maximum_records_touched,
        diagnostics.compile.records_touched);
    report->full_fallbacks += diagnostics.compile.full_fallbacks;
    report->maximum_pending_callbacks = std::max(
        report->maximum_pending_callbacks, scheduler->pending_callback_count());
    report->handoff_frames = std::max(report->handoff_frames, 1U);
  }

  // Selection/drag stays on a non-Stroke node while MoveStroke is undefined.
  const NodeRecord* selected = document->Find(1U);
  if (selected == nullptr || selected->type == NodeType::kStroke) {
    return SetError(error, "action-cycle ordinary selection target unavailable");
  }
  NodeRecord dragged = *selected;
  dragged.bounds.left += 8.0F;
  dragged.bounds.right += 8.0F;
  dragged.bounds.top += 4.0F;
  dragged.bounds.bottom += 4.0F;
  ++dragged.content_revision;
  ChangeSet drag_changes;
  if (!document->Apply({OperationKind::kUpdate, dragged.id, dragged},
                       &drag_changes, error)) {
    return false;
  }
  CompileDiagnostics drag_diagnostics;
  if (!SceneCompiler().ApplyIncremental(*document, drag_changes, scene,
                                        &drag_diagnostics, error)) {
    return false;
  }
  cache->InvalidateWorld(1U, drag_diagnostics.authoritative_world_dirty,
                         256.0F);
  report->maximum_records_touched = std::max(
      report->maximum_records_touched, drag_diagnostics.records_touched);
  report->full_fallbacks += drag_diagnostics.full_fallbacks;

  const std::array<ViewState, 3> action_views{
      ViewState{1U, 1U, 1U, Bounds{0.0F, 0.0F, 1920.0F, 1080.0F},
                1.0F, 1.0F, 1920U, 1080U},
      ViewState{1U, 2U, 1U,
                Bounds{12000.0F, 400.0F, 14133.333F, 1600.0F},
                0.9F, 1.0F, 1920U, 1080U},
      ViewState{1U, 3U, 1U, Bounds{2000.0F, 0.0F, 3280.0F, 720.0F},
                1.5F, 1.0F, 1920U, 1080U},
  };
  for (const ViewState& view : action_views) {
    const ViewQueryResult query = QueryView(*scene, view, std::nullopt);
    report->maximum_candidates = std::max(report->maximum_candidates,
                                           query.candidates.size());
    FrameGraph graph = BuildFrame(*scene, query, {});
    const std::string visual_digest = graph.VisualDigest();
    OptimizeFrameGraph(&graph);
    if (graph.VisualDigest() != visual_digest) {
      return SetError(error, "action-cycle FrameGraph optimization changed visuals");
    }
  }
  const RuntimeScene oracle = SceneCompiler().CompileFull(*document);
  if (scene->Digest() != oracle.Digest()) {
    return SetError(error, "action-cycle incremental/full Scene differs");
  }
  report->cache_invalidations = cache->stats().invalidations;
  report->vector_stroke_digest = poc02::StrokeDigest(*geometry->Find(ids[0]));
  report->dab_stroke_digest = poc02::StrokeDigest(*geometry->Find(ids[1]));
  report->ink_document_digest = geometry->document().Digest();
  report->document_digest = document->Digest();
  report->scene_digest = scene->Digest();
  if (error != nullptr) {
    error->clear();
  }
  return report->maximum_candidates <= 5000U &&
         report->maximum_records_touched <= 1U &&
         report->full_fallbacks == 0U &&
         report->cache_invalidations >= 2U &&
         scheduler->pending_callback_count() == 0U;
}

}  // namespace canvas::poc03
