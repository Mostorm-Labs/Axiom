#include "poc02_adapter.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include "canvas_poc02/ink_engine.h"

namespace canvas::poc06 {
namespace {

canvas::poc02::Status MapStatus(arc::Status status) {
  switch (status) {
    case arc::Status::kOk:
      return canvas::poc02::Status::kOk;
    case arc::Status::kInvalidArgument:
      return canvas::poc02::Status::kInvalidArgument;
    case arc::Status::kInvalidState:
      return canvas::poc02::Status::kInvalidState;
    case arc::Status::kStaleRevision:
      return canvas::poc02::Status::kStaleGeneration;
    case arc::Status::kNotFound:
      return canvas::poc02::Status::kNotFound;
    case arc::Status::kCapacityExceeded:
      return canvas::poc02::Status::kOk;
    case arc::Status::kAbiMismatch:
    case arc::Status::kBackendUnavailable:
    case arc::Status::kPresentationFailed:
    case arc::Status::kSurfaceLost:
    case arc::Status::kInternalError:
      // Arc presentation failures are deliberately not propagated as input
      // failures. The bridge switches to Null/Default fallback and reports OK.
      return canvas::poc02::Status::kOk;
  }
  return canvas::poc02::Status::kInvalidState;
}

uint32_t PrimitiveKind(const canvas::poc02::BrushDescriptor& brush) {
  return brush.type == canvas::poc02::BrushType::kDab
             ? ARC_PREVIEW_PRIMITIVE_DAB
             : ARC_PREVIEW_PRIMITIVE_VECTOR_POINT;
}

arc_brush_descriptor_v0 Brush(const canvas::poc02::BrushDescriptor& source) {
  return {.struct_size = sizeof(arc_brush_descriptor_v0),
          .abi_version = ARC_ABI_VERSION,
          .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
          .brush_type = PrimitiveKind(source),
          .brush_version = source.brush_version,
          .algorithm_version = source.algorithm_version,
          .size = source.size,
          .spacing = source.spacing,
          .opacity = source.opacity,
          .jitter = source.jitter,
          .resource_id = source.resource_id.data(),
          .resource_id_size = static_cast<uint32_t>(source.resource_id.size()),
          .resource_content_hash = source.resource_content_hash.data(),
          .resource_content_hash_size =
              static_cast<uint32_t>(source.resource_content_hash.size())};
}

arc_handoff_token_v0 Token(uint64_t stroke_id, uint64_t document_revision) {
  return {.high = document_revision, .low = stroke_id};
}

}  // namespace

class PreviewAdapter final : public canvas::poc02::PreviewSink {
 public:
  PreviewAdapter(arc::Bridge& bridge, uint64_t generation)
      : bridge_(bridge), generation_(generation) {}

  canvas::poc02::Status Begin(
      canvas::poc02::StrokeId id,
      const canvas::poc02::BrushDescriptor& brush) override {
    arc_preview_begin_v0 begin{
        .struct_size = sizeof(arc_preview_begin_v0),
        .abi_version = ARC_ABI_VERSION,
        .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
        .coordinate_space = ARC_COORDINATE_SPACE_WORLD,
        .stroke_id = id,
        .view_id = 1,
        .viewport_revision = 1,
        .target_generation = generation_,
        .brush = Brush(brush)};
    return MapStatus(bridge_.Begin(begin));
  }

  canvas::poc02::Status Push(
      const canvas::poc02::PreviewStrokeUpdate& update) override {
    std::vector<arc_preview_primitive_v0> confirmed;
    std::vector<arc_preview_primitive_v0> predicted;
    confirmed.reserve(update.confirmed_append.size());
    predicted.reserve(update.predicted_tail.size());
    const auto convert = [](const canvas::poc02::PreviewPrimitive& source,
                            uint32_t kind) {
      return arc_preview_primitive_v0{.kind = kind,
                                      .x = source.position.x,
                                      .y = source.position.y,
                                      .radius = source.radius,
                                      .rotation_degrees = source.rotation_degrees,
                                      .opacity = source.opacity};
    };
    const uint32_t kind = PrimitiveKind(update.brush);
    for (const auto& primitive : update.confirmed_append) {
      confirmed.push_back(convert(primitive, kind));
    }
    for (const auto& primitive : update.predicted_tail) {
      predicted.push_back(convert(primitive, kind));
    }
    arc_preview_update_v0 converted{
        .struct_size = sizeof(arc_preview_update_v0),
        .abi_version = ARC_ABI_VERSION,
        .schema_version = ARC_PROTOCOL_SCHEMA_VERSION,
        .coordinate_space = ARC_COORDINATE_SPACE_WORLD,
        .stroke_id = update.stroke_id,
        .preview_revision = update.revision,
        .view_id = update.view_id,
        .viewport_revision = update.viewport_revision,
        .target_generation = generation_,
        .truncate_confirmed_to = static_cast<uint32_t>(update.truncate_confirmed_to),
        .confirmed_append = confirmed.data(),
        .confirmed_append_count = static_cast<uint32_t>(confirmed.size()),
        .confirmed_append_stride = sizeof(arc_preview_primitive_v0),
        .predicted_tail = predicted.data(),
        .predicted_tail_count = static_cast<uint32_t>(predicted.size()),
        .predicted_tail_stride = sizeof(arc_preview_primitive_v0)};
    return MapStatus(bridge_.Push(converted));
  }

  canvas::poc02::Status CanonicalCommitted(
      canvas::poc02::StrokeId id, uint64_t document_revision) override {
    const arc::StrokeSnapshot* state = bridge_.Find(id);
    if (state == nullptr || state->preview_revision == 0) {
      return canvas::poc02::Status::kInvalidState;
    }
    const arc_preview_seal_v0 seal{
        .struct_size = sizeof(arc_preview_seal_v0),
        .abi_version = ARC_ABI_VERSION,
        .stroke_id = id,
        .final_preview_revision = state->preview_revision,
        .target_generation = generation_};
    const arc::Status seal_status = bridge_.SealInput(seal);
    if (seal_status != arc::Status::kOk) return MapStatus(seal_status);
    arc_canonical_commit_v0 commit{
        .struct_size = sizeof(arc_canonical_commit_v0),
        .abi_version = ARC_ABI_VERSION,
        .stroke_id = id,
        .final_preview_revision = state->preview_revision,
        .document_revision = document_revision,
        .target_generation = generation_,
        .handoff_token = Token(id, document_revision)};
    return MapStatus(bridge_.CanonicalCommitted(commit));
  }

  canvas::poc02::Status CanonicalVisible(
      canvas::poc02::StrokeId id, uint64_t document_revision) override {
    arc_canonical_visible_v0 visible{
        .struct_size = sizeof(arc_canonical_visible_v0),
        .abi_version = ARC_ABI_VERSION,
        .stroke_id = id,
        .document_revision = document_revision,
        .target_generation = generation_,
        .handoff_token = Token(id, document_revision),
        .receipt = {.struct_size = sizeof(arc_presentation_receipt_v0),
                    .abi_version = ARC_ABI_VERSION,
                    .evidence = ARC_EVIDENCE_DETERMINISTIC_ORACLE,
                    .status = ARC_STATUS_OK,
                    .target_generation = generation_}};
    return MapStatus(bridge_.CanonicalVisible(visible));
  }

  canvas::poc02::Status Cancel(canvas::poc02::StrokeId id) override {
    return MapStatus(bridge_.Cancel({
        .struct_size = sizeof(arc_preview_cancel_v0),
        .abi_version = ARC_ABI_VERSION,
        .stroke_id = id,
        .target_generation = generation_,
        .reason = 1}));
  }

  void SetGeneration(uint64_t generation) { generation_ = generation; }

 private:
  arc::Bridge& bridge_;
  uint64_t generation_;
};

std::unique_ptr<canvas::poc02::PreviewSink> CreateArcPreviewAdapter(
    arc::Bridge& bridge, uint64_t target_generation) {
  return std::make_unique<PreviewAdapter>(bridge, target_generation);
}

}  // namespace canvas::poc06
