#include "canvas/embed/embedded_surface.h"

#include <unordered_set>
#include <utility>
#include <variant>

namespace canvas::embed {

void EmbeddedSurfaceManager::sync(
    const document::Document& document, core::Rect viewport,
    const std::optional<document::NodeId>& activeNode) {
  const auto isRequired = [&](const document::Node& node) {
    if (node.layer != document::LayerClass::Embedded ||
        !node.bounds.intersects(viewport)) {
      return false;
    }

    const auto* embedded = std::get_if<document::EmbeddedNode>(&node.payload);
    if (embedded == nullptr) {
      return false;
    }

    const bool isActive = activeNode && *activeNode == node.id;
    const bool activeDocumentSurface =
        embedded->kind == document::EmbeddedKind::Web ||
        embedded->kind == document::EmbeddedKind::RichText;
    return embedded->kind == document::EmbeddedKind::Video ||
           (activeDocumentSurface && isActive);
  };

  std::unordered_set<document::NodeId> required;
  for (const auto& node : document.nodes()) {
    if (isRequired(node)) {
      required.insert(node.id);
    }
  }

  for (auto live = live_.begin(); live != live_.end();) {
    if (required.find(live->first) == required.end()) {
      live = live_.erase(live);
    } else {
      ++live;
    }
  }

  for (const auto& node : document.nodes()) {
    if (!isRequired(node)) {
      continue;
    }

    const bool isActive = activeNode && *activeNode == node.id;
    auto live = live_.find(node.id);
    if (live != live_.end()) {
      live->second->setBounds(node.bounds);
      live->second->setInteractive(isActive);
      live->second->setVisible(true);
      continue;
    }

    auto surface = factory_.create(node);
    if (!surface) {
      // No surface is available yet. Keep the entry absent so a later sync
      // can retry without ever storing or dereferencing a null surface.
      continue;
    }

    // Configure before emplacing so a setter exception destroys the local
    // surface and never leaves a partially configured map entry.
    surface->setBounds(node.bounds);
    surface->setInteractive(isActive);
    surface->setVisible(true);
    live_.emplace(node.id, std::move(surface));
  }
}

}  // namespace canvas::embed
