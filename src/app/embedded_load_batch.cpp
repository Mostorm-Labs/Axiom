#include "canvas/app/embedded_load_batch.h"

#include <utility>

namespace canvas::app {

std::optional<EmbeddedLoadBatch> EmbeddedLoadBatch::create(
    std::uint64_t generation, std::vector<Load> loads) {
  if (generation == 0U || loads.size() > maximumLoadCount) return std::nullopt;
  for (std::size_t index = 0; index < loads.size(); ++index) {
    if (loads[index].token == 0U) return std::nullopt;
    for (std::size_t previous = 0; previous < index; ++previous)
      if (loads[previous].token == loads[index].token) return std::nullopt;
  }
  return EmbeddedLoadBatch(generation, std::move(loads));
}

EmbeddedLoadBatch::EmbeddedLoadBatch(std::uint64_t generation,
                                     std::vector<Load> loads) noexcept
    : documentGeneration_(generation), loads_(std::move(loads)),
      remaining_(loads_.size()),
      state_(loads_.empty() ? State::Ready : State::Pending) {}

bool EmbeddedLoadBatch::complete(Token token, std::uint64_t generation,
                                 Completion completion) noexcept {
  if (state_ != State::Pending || generation != documentGeneration_ || token == 0U)
    return false;
  for (std::size_t index = 0; index < loads_.size(); ++index) {
    if (loads_[index].token != token || completed_.test(index)) continue;
    if (completion == Completion::Ready) {
      completed_.set(index);
      --remaining_;
      if (remaining_ == 0U) state_ = State::Ready;
      return true;
    }
    if (completion == Completion::Failed) {
      failedIndex_ = index;
      state_ = State::Failed;
      return true;
    }
    return false;
  }
  return false;
}

bool EmbeddedLoadBatch::cancel() noexcept {
  if (state_ != State::Pending) return false;
  state_ = State::Cancelled;
  return true;
}

bool EmbeddedLoadBatch::timeout() noexcept {
  if (state_ != State::Pending) return false;
  state_ = State::Timeout;
  return true;
}

const EmbeddedLoadBatch::Load* EmbeddedLoadBatch::failedLoad() const noexcept {
  if (!failedIndex_ || *failedIndex_ >= loads_.size()) return nullptr;
  return &loads_[*failedIndex_];
}

}  // namespace canvas::app
