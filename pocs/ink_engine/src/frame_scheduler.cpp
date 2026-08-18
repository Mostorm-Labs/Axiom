#include "canvas_poc02/ink_engine.h"

#include <algorithm>

namespace canvas::poc02 {

void DeterministicFrameScheduler::Invalidate(const FrameInvalidation& invalidation) {
  auto [iterator, inserted] = pending_.try_emplace(invalidation.view_id, invalidation);
  if (inserted) return;
  FrameInvalidation& pending = iterator->second;
  pending.reasons |= invalidation.reasons;
  pending.minimum_document_revision =
      std::max(pending.minimum_document_revision,
               invalidation.minimum_document_revision);
  pending.minimum_preview_revision =
      std::max(pending.minimum_preview_revision,
               invalidation.minimum_preview_revision);
  pending.target_generation =
      std::max(pending.target_generation, invalidation.target_generation);
}

void DeterministicFrameScheduler::SetTargetGeneration(ViewId view_id,
                                                       uint64_t generation) {
  generations_[view_id] = generation;
  auto iterator = pending_.find(view_id);
  if (iterator != pending_.end()) {
    iterator->second.target_generation = generation;
    iterator->second.reasons |= static_cast<uint32_t>(FrameInvalidationReason::kRecovery);
  }
}

std::optional<FrameInvalidation> DeterministicFrameScheduler::BeginFrame(ViewId view_id) {
  auto iterator = pending_.find(view_id);
  if (iterator == pending_.end()) return std::nullopt;
  FrameInvalidation result = iterator->second;
  pending_.erase(iterator);
  const auto generation = generations_.find(view_id);
  if (generation != generations_.end()) result.target_generation = generation->second;
  return result;
}

Status DeterministicFrameScheduler::Present(const PresentedFrame& frame) {
  const auto generation = generations_.find(frame.view_id);
  if (generation == generations_.end() || generation->second != frame.target_generation) {
    return Status::kStaleGeneration;
  }
  const auto prior = presented_.find(frame.view_id);
  if (prior != presented_.end() &&
      (frame.document_revision < prior->second.document_revision ||
       frame.preview_revision < prior->second.preview_revision)) {
    return Status::kInvalidArgument;
  }
  presented_[frame.view_id] = frame;
  return Status::kOk;
}

size_t DeterministicFrameScheduler::pending_callback_count(ViewId view_id) const {
  return pending_.contains(view_id) ? 1U : 0U;
}

const PresentedFrame* DeterministicFrameScheduler::LastPresented(ViewId view_id) const {
  const auto iterator = presented_.find(view_id);
  return iterator == presented_.end() ? nullptr : &iterator->second;
}

}  // namespace canvas::poc02
