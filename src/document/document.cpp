#include "canvas/document/document.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace canvas::document {

namespace {
std::uint64_t nextDocumentId() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace

Document::Document() : instanceId_(nextDocumentId()) {}

Document::Document(const Document& other)
    : nodes_(other.nodes_), instanceId_(nextDocumentId()),
      generation_(other.generation_) {}

Document& Document::operator=(const Document& other) {
  if (this != &other) {
    nodes_ = other.nodes_;
    instanceId_ = nextDocumentId();
    generation_ = other.generation_;
  }
  return *this;
}

Document::Document(Document&& other) noexcept
    : nodes_(std::move(other.nodes_)), instanceId_(nextDocumentId()),
      generation_(other.generation_) {}

Document& Document::operator=(Document&& other) noexcept {
  if (this != &other) {
    nodes_ = std::move(other.nodes_);
    instanceId_ = nextDocumentId();
    generation_ = other.generation_;
  }
  return *this;
}

const Node* Document::find(std::string_view id) const {
  const auto it = std::find_if(
      nodes_.begin(), nodes_.end(),
      [id](const Node& node) { return std::string_view(node.id) == id; });
  return it == nodes_.end() ? nullptr : &*it;
}

bool Document::mutate(std::string_view id,
                      const std::function<void(Node&)>& mutation) {
  const auto it = std::find_if(
      nodes_.begin(), nodes_.end(),
      [id](const Node& node) { return std::string_view(node.id) == id; });
  if (it == nodes_.end() || !mutation) return false;
  mutation(*it);
  ++it->revision;
  ++it->nonAppendRevision;
  return true;
}

bool Document::appendStrokePoint(std::string_view id, StrokePoint point,
                                 core::Rect dirtyBounds) {
  const auto it = std::find_if(
      nodes_.begin(), nodes_.end(),
      [id](const Node& node) { return std::string_view(node.id) == id; });
  if (it == nodes_.end()) return false;
  auto* stroke = std::get_if<StrokeNode>(&it->payload);
  if (stroke == nullptr) return false;
  stroke->points.push_back(std::move(point));
  if (dirtyBounds.width > 0.0F && dirtyBounds.height > 0.0F) {
    it->bounds = it->bounds.united(dirtyBounds);
  }
  ++it->revision;
  return true;
}

bool Document::add(Node node) {
  if (node.id.empty() || find(node.id) != nullptr) {
    return false;
  }
  if (node.parentId && find(*node.parentId) == nullptr) {
    return false;
  }
  nodes_.push_back(std::move(node));
  ++generation_;
  return true;
}

bool Document::setBounds(std::string_view id, core::Rect bounds) {
  if (!(bounds.width > 0.0F) || !(bounds.height > 0.0F)) {
    return false;
  }
  return mutate(id, [bounds](Node& node) { node.bounds = bounds; });
}

bool Document::erase(std::string_view id) {
  const NodeId key{id};
  const auto originalSize = nodes_.size();
  nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                              [key](const Node& node) {
                                return node.id == key ||
                                       (node.parentId &&
                                        *node.parentId == key);
                              }),
               nodes_.end());
  if (nodes_.size() != originalSize) ++generation_;
  return nodes_.size() != originalSize;
}

}  // namespace canvas::document
