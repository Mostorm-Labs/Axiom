#include "canvas/document/document.h"

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <utility>

namespace canvas::document {

namespace {
std::uint64_t nextDocumentId() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}
std::uint64_t nextNodeIdentity() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace

Document::Document() : instanceId_(nextDocumentId()) {}

Document::Document(const Document& other)
    : nodes_(other.nodes_), instanceId_(nextDocumentId()) {}

Document& Document::operator=(const Document& other) {
  if (this != &other) {
    nodes_ = other.nodes_;
    instanceId_ = nextDocumentId();
  }
  return *this;
}

Document::Document(Document&& other) noexcept
    : nodes_(std::move(other.nodes_)), instanceId_(nextDocumentId()) {}

Document& Document::operator=(Document&& other) noexcept {
  if (this != &other) {
    nodes_ = std::move(other.nodes_);
    instanceId_ = nextDocumentId();
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
  node.cacheIdentity = nextNodeIdentity();
  nodes_.push_back(std::move(node));
  return true;
}

bool Document::replaceValidatedNodes(std::vector<Node> nodes) {
  std::unordered_map<NodeId, std::size_t> indexes;
  indexes.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (nodes[index].id.empty() ||
        !indexes.emplace(nodes[index].id, index).second) {
      return false;
    }
  }
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (!nodes[index].parentId) continue;
    const auto parent = indexes.find(*nodes[index].parentId);
    if (parent == indexes.end() || parent->second == index) return false;
  }

  // Parent links are a forest. Detect cycles iteratively so loading a deep
  // but bounded document never consumes the native call stack.
  std::vector<unsigned char> marks(nodes.size(), 0);
  std::vector<std::size_t> path;
  path.reserve(64);
  for (std::size_t start = 0; start < nodes.size(); ++start) {
    if (marks[start] == 2) continue;
    path.clear();
    std::size_t current = start;
    while (marks[current] == 0) {
      marks[current] = 1;
      path.push_back(current);
      if (!nodes[current].parentId) break;
      current = indexes.at(*nodes[current].parentId);
    }
    const bool terminalInPath =
        marks[current] == 1 && !nodes[current].parentId;
    if (marks[current] == 1 && !terminalInPath) {
      return false;
    }
    for (const auto index : path) marks[index] = 2;
  }

  for (auto& node : nodes) node.cacheIdentity = nextNodeIdentity();
  nodes_ = std::move(nodes);
  instanceId_ = nextDocumentId();
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
  return nodes_.size() != originalSize;
}

}  // namespace canvas::document
