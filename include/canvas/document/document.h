#pragma once

#include "canvas/document/node.h"

#include <string_view>
#include <functional>
#include <vector>

namespace canvas::document {

class Document {
 public:
  Document();
  Document(const Document& other);
  Document& operator=(const Document& other);
  Document(Document&& other) noexcept;
  Document& operator=(Document&& other) noexcept;

  static constexpr int schemaVersion = 1;

  bool add(Node node);
  bool erase(std::string_view id);
  bool setBounds(std::string_view id, core::Rect bounds);
  const Node* find(std::string_view id) const;
  // Generic edits invalidate cached geometry; callers changing stroke
  // geometry must update Node::bounds in the mutation. Use
  // appendStrokePoint() for the latency-sensitive append-only path.
  bool mutate(std::string_view id, const std::function<void(Node&)>& mutation);
  bool appendStrokePoint(std::string_view id, StrokePoint point,
                         core::Rect dirtyBounds);
  const std::vector<Node>& nodes() const { return nodes_; }
  std::uint64_t instanceId() const noexcept { return instanceId_; }

 private:
  std::vector<Node> nodes_;
  std::uint64_t instanceId_ = 0;
};

}  // namespace canvas::document
