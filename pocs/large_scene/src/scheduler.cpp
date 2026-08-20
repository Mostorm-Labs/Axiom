#include "canvas/poc03/large_scene.h"

#include <algorithm>

namespace canvas::poc03 {

void DeterministicFrameScheduler::Invalidate(
    const FrameInvalidation& invalidation) {
  auto [iterator, inserted] = pending_.try_emplace(invalidation.view_id,
                                                   invalidation);
  if (inserted) {
    return;
  }
  FrameInvalidation& pending = iterator->second;
  pending.minimum_document_revision = std::max(
      pending.minimum_document_revision,
      invalidation.minimum_document_revision);
  pending.minimum_view_revision = std::max(pending.minimum_view_revision,
                                           invalidation.minimum_view_revision);
  pending.minimum_preview_revision = std::max(
      pending.minimum_preview_revision,
      invalidation.minimum_preview_revision);
  pending.target_generation = std::max(pending.target_generation,
                                       invalidation.target_generation);
  pending.reason_mask |= invalidation.reason_mask;
}

size_t DeterministicFrameScheduler::pending_callback_count() const {
  return pending_.size();
}

std::optional<FrameInvalidation> DeterministicFrameScheduler::Pump(
    uint64_t view_id, uint64_t target_generation) {
  const auto found = pending_.find(view_id);
  if (found == pending_.end()) {
    return std::nullopt;
  }
  if (found->second.target_generation != target_generation) {
    return std::nullopt;
  }
  FrameInvalidation frame = found->second;
  pending_.erase(found);
  return frame;
}

bool DeterministicFrameScheduler::Present(const FrameInvalidation& frame,
                                          uint64_t target_generation) {
  if (frame.target_generation != target_generation) {
    return false;
  }
  presented_[frame.view_id] = std::max(presented_[frame.view_id],
                                       frame.minimum_document_revision);
  return true;
}

void DeterministicFrameScheduler::DestroyView(uint64_t view_id) {
  pending_.erase(view_id);
  presented_.erase(view_id);
}

uint64_t DeterministicFrameScheduler::last_presented_revision(
    uint64_t view_id) const {
  const auto found = presented_.find(view_id);
  return found == presented_.end() ? 0U : found->second;
}

}  // namespace canvas::poc03
