#include "canvas/app/embedded_document_open_coordinator.h"

#include <utility>

namespace canvas::app {

bool EmbeddedDocumentOpenCoordinator::begin(Candidate candidate) {
  auto batch = EmbeddedLoadBatch::create(candidate.generation,
                                          std::move(candidate.loads));
  if (!batch) return false;
  state_ = State::Pending;
  pendingGeneration_ = candidate.generation;
  pendingNodeCount_ = candidate.nodeCount;
  batch_ = std::move(*batch);
  if (batch_->state() == EmbeddedLoadBatch::State::Ready) {
    state_ = State::Ready;
    committedGeneration_ = pendingGeneration_;
    committedNodeCount_ = pendingNodeCount_;
    pendingGeneration_ = 0U;
    batch_.reset();
  }
  return true;
}

bool EmbeddedDocumentOpenCoordinator::complete(
    std::uint64_t generation, EmbeddedLoadBatch::Token token,
    bool success) noexcept {
  if (state_ != State::Pending || !batch_ || generation != pendingGeneration_)
    return false;
  const auto result = batch_->complete(
      token, generation,
      success ? EmbeddedLoadBatch::Completion::Ready
              : EmbeddedLoadBatch::Completion::Failed);
  if (!result) return false;
  if (!success) {
    state_ = State::Failed;
    return true;
  }
  if (batch_->state() == EmbeddedLoadBatch::State::Ready) {
    state_ = State::Ready;
    committedGeneration_ = pendingGeneration_;
    committedNodeCount_ = pendingNodeCount_;
    pendingGeneration_ = 0U;
    batch_.reset();
  }
  return true;
}

bool EmbeddedDocumentOpenCoordinator::cancel() noexcept {
  if (state_ != State::Pending || !batch_) return false;
  (void)batch_->cancel();
  state_ = State::Cancelled;
  pendingGeneration_ = 0U;
  return true;
}

bool EmbeddedDocumentOpenCoordinator::timeout() noexcept {
  if (state_ != State::Pending || !batch_) return false;
  (void)batch_->timeout();
  state_ = State::Timeout;
  pendingGeneration_ = 0U;
  return true;
}

std::uint64_t EmbeddedDocumentOpenCoordinator::pendingGeneration() const noexcept {
  return pendingGeneration_;
}

}  // namespace canvas::app
