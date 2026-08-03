#include "canvas/input/input_router.h"

namespace canvas::input {

RouteResult InputRouter::route(
    PointerKind kind,
    const std::optional<document::NodeId>& hitEmbedded) const {
  const bool drawingPointer =
      kind == PointerKind::Pen ||
      (kind == PointerKind::Touch && fingerDrawEnabled_ &&
       mode_ == InputMode::Draw) ||
      (kind == PointerKind::Mouse && mouseDrawEnabled_ &&
       mode_ == InputMode::Draw);

  if (drawingPointer) {
    if (hitEmbedded) {
      return {InputTarget::Annotation, hitEmbedded};
    }
    return {InputTarget::BaseCanvas, std::nullopt};
  }

  if (mode_ == InputMode::Interact && hitEmbedded &&
      activeEmbedded_ == hitEmbedded) {
    return {InputTarget::EmbeddedSurface, hitEmbedded};
  }

  if (mode_ == InputMode::Select) {
    return {InputTarget::Selection, hitEmbedded};
  }

  return {InputTarget::Viewport, std::nullopt};
}

}  // namespace canvas::input
