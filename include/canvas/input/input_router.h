#pragma once

#include "canvas/document/node.h"
#include "canvas/input/pointer_sample.h"

#include <optional>
#include <utility>

namespace canvas::input {

enum class InputMode { Draw, Select, Interact };
enum class InputTarget {
  BaseCanvas,
  Annotation,
  Selection,
  EmbeddedSurface,
  Viewport,
};

struct RouteResult {
  InputTarget target = InputTarget::Viewport;
  std::optional<document::NodeId> parentId;
};

class InputRouter {
 public:
  void setMode(InputMode mode) { mode_ = mode; }
  void setFingerDrawEnabled(bool enabled) { fingerDrawEnabled_ = enabled; }
  void setMouseDrawEnabled(bool enabled) { mouseDrawEnabled_ = enabled; }
  void setActiveEmbeddedNode(std::optional<document::NodeId> id) {
    activeEmbedded_ = std::move(id);
  }

  RouteResult route(
      PointerKind kind,
      const std::optional<document::NodeId>& hitEmbedded) const;

 private:
  InputMode mode_ = InputMode::Draw;
  bool fingerDrawEnabled_ = false;
  bool mouseDrawEnabled_ = false;
  std::optional<document::NodeId> activeEmbedded_;
};

}  // namespace canvas::input
