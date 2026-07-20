#include "canvas/document/document.h"

#include <algorithm>
#include <utility>

namespace canvas::document {

Node* Document::find(std::string_view id) {
  const auto it = std::find_if(
      nodes_.begin(), nodes_.end(),
      [id](const Node& node) { return std::string_view(node.id) == id; });
  return it == nodes_.end() ? nullptr : &*it;
}

const Node* Document::find(std::string_view id) const {
  const auto it = std::find_if(
      nodes_.begin(), nodes_.end(),
      [id](const Node& node) { return std::string_view(node.id) == id; });
  return it == nodes_.end() ? nullptr : &*it;
}

bool Document::add(Node node) {
  if (node.id.empty() || find(node.id) != nullptr) {
    return false;
  }
  if (node.parentId && find(*node.parentId) == nullptr) {
    return false;
  }
  nodes_.push_back(std::move(node));
  return true;
}

bool Document::setBounds(std::string_view id, core::Rect bounds) {
  Node* node = find(id);
  if (node == nullptr || bounds.width <= 0 || bounds.height <= 0) {
    return false;
  }
  node->bounds = bounds;
  return true;
}

bool Document::erase(std::string_view id) {
  const auto originalSize = nodes_.size();
  nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                              [id](const Node& node) {
                                return std::string_view(node.id) == id ||
                                       (node.parentId &&
                                        std::string_view(*node.parentId) == id);
                              }),
               nodes_.end());
  return nodes_.size() != originalSize;
}

}  // namespace canvas::document
