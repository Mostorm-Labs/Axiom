#pragma once

#include "canvas/app/embedded_load_batch.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace canvas::app {

// Platform-neutral transaction model used by WhiteboardApp integration tests.
// It deliberately owns no Document or native surface: a platform owner stages
// those objects separately and performs the real swap only after state()==Ready.
class EmbeddedDocumentOpenCoordinator final {
 public:
  enum class State { Idle, Pending, Ready, Failed, Cancelled, Timeout };
  struct Candidate {
    std::uint64_t generation = 0U;
    std::size_t nodeCount = 0U;
    std::vector<EmbeddedLoadBatch::Load> loads;
  };

  bool begin(Candidate candidate);
  bool complete(std::uint64_t generation, EmbeddedLoadBatch::Token token,
                bool success) noexcept;
  bool cancel() noexcept;
  bool timeout() noexcept;
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] std::uint64_t pendingGeneration() const noexcept;
  [[nodiscard]] std::uint64_t committedGeneration() const noexcept {
    return committedGeneration_;
  }
  [[nodiscard]] std::size_t committedNodeCount() const noexcept {
    return committedNodeCount_;
  }

 private:
  State state_ = State::Idle;
  std::uint64_t pendingGeneration_ = 0U;
  std::uint64_t committedGeneration_ = 0U;
  std::size_t committedNodeCount_ = 0U;
  std::size_t pendingNodeCount_ = 0U;
  std::optional<EmbeddedLoadBatch> batch_;
};

}  // namespace canvas::app
