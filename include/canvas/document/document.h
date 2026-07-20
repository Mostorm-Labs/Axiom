#pragma once

#include "canvas/document/node.h"

#include <string_view>
#include <vector>

namespace canvas::document {

class Document {
 public:
  static constexpr int schemaVersion = 1;

  bool add(Node node);
  bool erase(std::string_view id);
  bool setBounds(std::string_view id, core::Rect bounds);
  Node* find(std::string_view id);
  const Node* find(std::string_view id) const;
  const std::vector<Node>& nodes() const { return nodes_; }

 private:
  std::vector<Node> nodes_;
};

}  // namespace canvas::document
