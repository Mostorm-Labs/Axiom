#include "platform/macos/macos_whiteboard_input.h"

#include "canvas/document/embedded_transform.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace canvas::macos {

namespace {

bool hasArea(const core::Rect& bounds) noexcept {
  return bounds.width > 0.0F && bounds.height > 0.0F;
}

document::StrokePoint strokePoint(const input::PointerSample& sample) {
  return {sample.screenPosition, sample.pressure, sample.timestampMicros};
}

MacosWhiteboardInputResult ignoredResult(bool fullRedraw = false) {
  MacosWhiteboardInputResult result;
  result.fullRedraw = fullRedraw;
  return result;
}

}  // namespace

MacosWhiteboardInput::MacosWhiteboardInput(
    std::shared_ptr<document::Document> document)
    : document_(std::move(document)) {
  // Mouse drawing remains disabled by default in the shared router. Only this
  // macOS mouse controller opts in, preserving existing Windows behavior.
  router_.setMouseDrawEnabled(true);
}

bool MacosWhiteboardInput::isAcceptedSample(
    const input::PointerSample& sample) noexcept {
  return sample.pointerId != 0 &&
         sample.kind == input::PointerKind::Mouse && !sample.predicted &&
         std::isfinite(sample.screenPosition.x) &&
         std::isfinite(sample.screenPosition.y) &&
         std::isfinite(sample.pressure) && sample.pressure >= 0.0F &&
         sample.pressure <= 1.0F &&
         std::isfinite(sample.tiltXDegrees) &&
         std::isfinite(sample.tiltYDegrees);
}

std::optional<document::NodeId> MacosWhiteboardInput::topmostEmbeddedHit(
    core::Vec2 position) const {
  if (!document_) return std::nullopt;
  const auto& nodes = document_->nodes();
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    if (it->layer == document::LayerClass::Embedded &&
        std::holds_alternative<document::EmbeddedNode>(it->payload) &&
        it->bounds.contains(position)) {
      return it->id;
    }
  }
  return std::nullopt;
}

std::optional<document::NodeId> MacosWhiteboardInput::allocateStrokeId() {
  while (document_ && nextStrokeSequence_ != 0) {
    const std::uint64_t sequence = nextStrokeSequence_;
    nextStrokeSequence_ =
        sequence == std::numeric_limits<std::uint64_t>::max() ? 0
                                                              : sequence + 1;
    document::NodeId candidate =
        "macos-stroke-" + std::to_string(sequence);
    if (document_->find(candidate) == nullptr) return candidate;
  }
  return std::nullopt;
}

bool MacosWhiteboardInput::ownsActivePreviewIdentity() const noexcept {
  if (!document_ || !active_ || !active_->cacheIdentity) return false;
  if (document_->instanceId() != active_->documentInstanceId) return false;
  const document::Node* node = document_->find(active_->nodeId);
  return node != nullptr &&
         node->cacheIdentity == *active_->cacheIdentity;
}

bool MacosWhiteboardInput::activePreviewIsValid() const noexcept {
  if (!document_ || !active_ || !active_->cacheIdentity) return false;
  if (document_->instanceId() != active_->documentInstanceId) return false;
  const document::Node* node = document_->find(active_->nodeId);
  return node != nullptr &&
         node->cacheIdentity == *active_->cacheIdentity &&
         node->revision == active_->expectedRevision &&
         node->nonAppendRevision == active_->expectedNonAppendRevision &&
         node->layer == active_->layer &&
         node->bounds == active_->bounds &&
         node->parentId == active_->parentId &&
         std::holds_alternative<document::StrokeNode>(node->payload);
}

MacosWhiteboardInputResult MacosWhiteboardInput::consume(
    const input::PointerSample& sample) {
  if (!isAcceptedSample(sample)) return {};

  if (sample.phase == input::PointerPhase::Down) {
    if (active_) return {};
    return begin(sample);
  }
  if (!active_ || active_->pointerId != sample.pointerId) return {};

  switch (sample.phase) {
    case input::PointerPhase::Move:
      return move(sample);
    case input::PointerPhase::Up:
      return finish(sample);
    case input::PointerPhase::Cancel:
      return rollback(false);
    case input::PointerPhase::Down:
      break;
  }
  return {};
}

MacosWhiteboardInputResult MacosWhiteboardInput::begin(
    const input::PointerSample& sample) {
  if (!document_) return {};

  const auto hitEmbedded = topmostEmbeddedHit(sample.screenPosition);
  const input::RouteResult route =
      router_.route(input::PointerKind::Mouse, hitEmbedded);
  if (route.target != input::InputTarget::BaseCanvas &&
      route.target != input::InputTarget::Annotation) {
    return {};
  }

  const document::LayerClass layer =
      route.target == input::InputTarget::Annotation
          ? document::LayerClass::Annotation
          : document::LayerClass::Base;
  std::optional<document::NodeId> nodeId = allocateStrokeId();
  if (!nodeId) {
    MacosWhiteboardInputResult failed;
    failed.kind = MacosWhiteboardInputResultKind::Failed;
    failed.layer = layer;
    return failed;
  }

  active_.emplace(sample.pointerId, std::move(*nodeId), layer,
                  route.parentId, kStrokeWidth);
  active_->builder.begin(sample);
  active_->bounds = core::Rect::fromPoints(sample.screenPosition,
                                           sample.screenPosition)
                        .inflated(kStrokeWidth);

  document::StrokeNode preview;
  preview.points.reserve(kPreviewPointCapacity);
  preview.points.push_back(strokePoint(sample));
  preview.width = kStrokeWidth;

  document::Node node;
  node.id = active_->nodeId;
  node.layer = layer;
  node.bounds = active_->bounds;
  node.parentId = active_->parentId;
  node.payload = std::move(preview);
  if (!document_->add(std::move(node))) return failActive(true);
  const document::Node* added = document_->find(active_->nodeId);
  if (added == nullptr) return failActive(true);
  active_->documentInstanceId = document_->instanceId();
  active_->cacheIdentity = added->cacheIdentity;
  active_->expectedRevision = added->revision;
  active_->expectedNonAppendRevision = added->nonAppendRevision;
  if (!activePreviewIsValid()) return failActive(true);

  MacosWhiteboardInputResult result;
  result.kind = MacosWhiteboardInputResultKind::Began;
  result.layer = layer;
  result.dirtyBounds = active_->bounds;
  return result;
}

MacosWhiteboardInputResult MacosWhiteboardInput::move(
    const input::PointerSample& sample) {
  if (!activePreviewIsValid()) return failActive(true);

  const stroke::StrokeUpdate update = active_->builder.append(sample);
  if (update.accepted &&
      !document_->appendStrokePoint(active_->nodeId, strokePoint(sample),
                                    update.dirtyBounds)) {
    return failActive(true);
  }
  if (update.accepted) {
    if (hasArea(update.dirtyBounds)) {
      active_->bounds = active_->bounds.united(update.dirtyBounds);
    }
    ++active_->expectedRevision;
    if (!activePreviewIsValid()) return failActive(true);
  }
  if (!update.accepted && !hasArea(update.dirtyBounds)) return {};

  MacosWhiteboardInputResult result;
  result.kind = MacosWhiteboardInputResultKind::Changed;
  result.layer = active_->layer;
  if (hasArea(update.dirtyBounds)) result.dirtyBounds = update.dirtyBounds;
  return result;
}

MacosWhiteboardInputResult MacosWhiteboardInput::finish(
    const input::PointerSample& sample) {
  if (!activePreviewIsValid()) return failActive(true);

  const stroke::StrokeUpdate update = active_->builder.append(sample);
  if (update.accepted &&
      !document_->appendStrokePoint(active_->nodeId, strokePoint(sample),
                                    update.dirtyBounds)) {
    return failActive(true);
  }
  if (update.accepted) {
    if (hasArea(update.dirtyBounds)) {
      active_->bounds = active_->bounds.united(update.dirtyBounds);
    }
    ++active_->expectedRevision;
    if (!activePreviewIsValid()) return failActive(true);
  }

  document::StrokeNode completed = active_->builder.finish();
  if (active_->layer == document::LayerClass::Annotation) {
    if (!active_->parentId) return failActive();
    const document::Node* parent = document_->find(*active_->parentId);
    if (parent == nullptr || parent->layer != document::LayerClass::Embedded ||
        !std::holds_alternative<document::EmbeddedNode>(parent->payload)) {
      return failActive();
    }
    try {
      completed =
          document::attachStrokeToParent(std::move(completed), parent->bounds);
    } catch (const std::domain_error&) {
      return failActive();
    }
  }

  if (!activePreviewIsValid()) return failActive(true);

  const document::LayerClass layer = active_->layer;
  const core::Rect bounds = active_->bounds;
  const std::optional<document::NodeId> parentId = active_->parentId;
  const bool stored = document_->mutate(
      active_->nodeId, [&](document::Node& node) {
        node.layer = layer;
        node.bounds = bounds;
        node.parentId = parentId;
        node.payload = std::move(completed);
      });
  if (!stored) return failActive();

  active_.reset();
  MacosWhiteboardInputResult result;
  result.kind = MacosWhiteboardInputResultKind::Finished;
  result.layer = layer;
  result.dirtyBounds = bounds;
  return result;
}

MacosWhiteboardInputResult MacosWhiteboardInput::rollback(bool fullRedraw) {
  if (!active_) return ignoredResult(fullRedraw);
  const document::LayerClass layer = active_->layer;
  const core::Rect dirtyBounds = active_->bounds;
  const bool identityOwned = ownsActivePreviewIdentity();
  const bool stateValid = identityOwned && activePreviewIsValid();
  const bool erased = identityOwned && document_->erase(active_->nodeId);
  active_.reset();

  MacosWhiteboardInputResult result;
  result.kind = stateValid && erased
                    ? MacosWhiteboardInputResultKind::Cancelled
                    : MacosWhiteboardInputResultKind::Failed;
  result.layer = layer;
  result.dirtyBounds = dirtyBounds;
  result.fullRedraw = fullRedraw || !stateValid || !erased;
  return result;
}

MacosWhiteboardInputResult MacosWhiteboardInput::failActive(bool fullRedraw) {
  if (!active_) {
    MacosWhiteboardInputResult result;
    result.kind = MacosWhiteboardInputResultKind::Failed;
    result.fullRedraw = true;
    return result;
  }
  const document::LayerClass layer = active_->layer;
  const core::Rect dirtyBounds = active_->bounds;
  const bool owned = ownsActivePreviewIdentity();
  const bool erased = owned && document_->erase(active_->nodeId);
  active_.reset();

  MacosWhiteboardInputResult result;
  result.kind = MacosWhiteboardInputResultKind::Failed;
  result.layer = layer;
  result.dirtyBounds = dirtyBounds;
  result.fullRedraw = fullRedraw || !owned || !erased;
  return result;
}

MacosWhiteboardInputResult MacosWhiteboardInput::setMode(
    input::InputMode mode) {
  if (mode == mode_) return {};
  MacosWhiteboardInputResult result = active_ ? rollback(false)
                                               : MacosWhiteboardInputResult{};
  mode_ = mode;
  router_.setMode(mode);
  return result;
}

MacosWhiteboardInputResult MacosWhiteboardInput::replaceDocument(
    std::shared_ptr<document::Document> document) {
  if (!document) {
    MacosWhiteboardInputResult result;
    result.kind = MacosWhiteboardInputResultKind::Failed;
    return result;
  }
  if (document == document_) return {};
  MacosWhiteboardInputResult result =
      active_ ? rollback(true) : ignoredResult(true);
  document_ = std::move(document);
  return result;
}

bool MacosWhiteboardInput::active() const noexcept {
  return active_.has_value();
}

const std::shared_ptr<document::Document>& MacosWhiteboardInput::document()
    const noexcept {
  return document_;
}

}  // namespace canvas::macos
