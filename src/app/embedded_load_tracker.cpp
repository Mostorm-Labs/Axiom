#include "canvas/app/embedded_load_tracker.h"

#include <limits>
#include <utility>

namespace canvas::app {

#if defined(CANVAS_EMBEDDED_LOAD_TRACKER_TEST_SEAM)
EmbeddedLoadTracker::EmbeddedLoadTracker(
    Token nextToken, std::pmr::memory_resource& resource) noexcept
    : nextToken_(nextToken == 0U ? 1U : nextToken), records_(&resource) {}
#endif

std::optional<EmbeddedLoadTracker::Token> EmbeddedLoadTracker::begin(
    const document::NodeId& nodeId, const std::string& requestId,
    std::uint64_t connectionId, std::uint64_t documentGeneration) noexcept {
  if (tokensExhausted_) return std::nullopt;

  // Reserve the token before any allocation. If construction or insertion
  // fails, this token is deliberately burned rather than reused by a later
  // asynchronous operation.
  const Token token = nextToken_;
  if (nextToken_ == std::numeric_limits<Token>::max()) {
    tokensExhausted_ = true;
  } else {
    ++nextToken_;
  }

  try {
    const auto inserted =
        records_
            .emplace(token, EmbeddedLoadRecord{token, nodeId, requestId,
                                                connectionId,
                                                documentGeneration})
            .second;
    if (!inserted) return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }

  return token;
}

std::optional<EmbeddedLoadRecord> EmbeddedLoadTracker::consume(
    Token token, std::uint64_t currentDocumentGeneration) noexcept {
  const auto found = records_.find(token);
  if (found == records_.end()) return std::nullopt;

  EmbeddedLoadRecord record = std::move(found->second);
  records_.erase(found);
  if (record.documentGeneration != currentDocumentGeneration) {
    return std::nullopt;
  }
  return record;
}

bool EmbeddedLoadTracker::cancel(Token token) noexcept {
  return records_.erase(token) != 0U;
}

std::size_t EmbeddedLoadTracker::cancelNode(
    const document::NodeId& nodeId) noexcept {
  std::size_t cancelled = 0;
  for (auto record = records_.begin(); record != records_.end();) {
    if (record->second.nodeId == nodeId) {
      record = records_.erase(record);
      ++cancelled;
    } else {
      ++record;
    }
  }
  return cancelled;
}

std::size_t EmbeddedLoadTracker::cancelGeneration(
    std::uint64_t documentGeneration) noexcept {
  std::size_t cancelled = 0;
  for (auto record = records_.begin(); record != records_.end();) {
    if (record->second.documentGeneration == documentGeneration) {
      record = records_.erase(record);
      ++cancelled;
    } else {
      ++record;
    }
  }
  return cancelled;
}

void EmbeddedLoadTracker::cancelAll() noexcept { records_.clear(); }

}  // namespace canvas::app
