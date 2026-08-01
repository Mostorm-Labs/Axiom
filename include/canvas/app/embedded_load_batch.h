#pragma once

#include "canvas/document/node.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace canvas::app {

class EmbeddedLoadBatch {
 public:
  using Token = std::uint64_t;
  static constexpr std::size_t maximumLoadCount = 256U;
  struct Load { Token token = 0; document::NodeId nodeId; };
  enum class Completion { Ready, Failed };
  enum class State { Pending, Ready, Failed, Cancelled, Timeout };

  [[nodiscard]] static std::optional<EmbeddedLoadBatch> create(
      std::uint64_t documentGeneration, std::vector<Load> loads);
  EmbeddedLoadBatch(const EmbeddedLoadBatch&) = delete;
  EmbeddedLoadBatch& operator=(const EmbeddedLoadBatch&) = delete;
  EmbeddedLoadBatch(EmbeddedLoadBatch&&) noexcept = default;
  EmbeddedLoadBatch& operator=(EmbeddedLoadBatch&&) noexcept = default;
  [[nodiscard]] std::uint64_t documentGeneration() const noexcept { return documentGeneration_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool isTerminal() const noexcept { return state_ != State::Pending; }
  [[nodiscard]] std::size_t size() const noexcept { return loads_.size(); }
  [[nodiscard]] std::size_t remaining() const noexcept { return remaining_; }
  [[nodiscard]] bool complete(Token token, std::uint64_t generation,
                              Completion completion) noexcept;
  [[nodiscard]] bool cancel() noexcept;
  [[nodiscard]] bool timeout() noexcept;
  [[nodiscard]] const Load* failedLoad() const noexcept;

 private:
  EmbeddedLoadBatch(std::uint64_t generation, std::vector<Load> loads) noexcept;
  std::uint64_t documentGeneration_ = 0;
  std::vector<Load> loads_;
  std::bitset<maximumLoadCount> completed_;
  std::optional<std::size_t> failedIndex_;
  std::size_t remaining_ = 0;
  State state_ = State::Pending;
};

}  // namespace canvas::app
