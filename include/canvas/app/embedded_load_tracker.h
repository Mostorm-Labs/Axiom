#pragma once

#include "canvas/document/node.h"

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string>
#include <unordered_map>

namespace canvas::app {

#if defined(CANVAS_EMBEDDED_LOAD_TRACKER_TEST_SEAM)
class EmbeddedLoadTrackerTestAccess;
#endif

struct EmbeddedLoadRecord {
  std::uint64_t token = 0;
  document::NodeId nodeId;
  std::string requestId;
  std::uint64_t connectionId = 0;
  std::uint64_t documentGeneration = 0;
};

class EmbeddedLoadTracker {
 public:
  using Token = std::uint64_t;

  EmbeddedLoadTracker() = default;
  EmbeddedLoadTracker(const EmbeddedLoadTracker&) = delete;
  EmbeddedLoadTracker& operator=(const EmbeddedLoadTracker&) = delete;
  EmbeddedLoadTracker(EmbeddedLoadTracker&&) = delete;
  EmbeddedLoadTracker& operator=(EmbeddedLoadTracker&&) = delete;

  [[nodiscard]] std::optional<Token> begin(
      const document::NodeId& nodeId, const std::string& requestId,
      std::uint64_t connectionId,
      std::uint64_t documentGeneration) noexcept;

  // A terminal callback owns the record once it calls consume. A callback for
  // an obsolete document generation must also retire the token, but receives
  // no record to act on.
  [[nodiscard]] std::optional<EmbeddedLoadRecord> consume(
      Token token, std::uint64_t currentDocumentGeneration) noexcept;

  [[nodiscard]] bool cancel(Token token) noexcept;
  [[nodiscard]] std::size_t cancelNode(
      const document::NodeId& nodeId) noexcept;
  [[nodiscard]] std::size_t cancelGeneration(
      std::uint64_t documentGeneration) noexcept;
  void cancelAll() noexcept;

 private:
#if defined(CANVAS_EMBEDDED_LOAD_TRACKER_TEST_SEAM)
  friend class EmbeddedLoadTrackerTestAccess;

  EmbeddedLoadTracker(Token nextToken,
                      std::pmr::memory_resource& resource) noexcept;
#endif

  Token nextToken_ = 1;
  bool tokensExhausted_ = false;
  std::pmr::unordered_map<Token, EmbeddedLoadRecord> records_;
};

}  // namespace canvas::app
