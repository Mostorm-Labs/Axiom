#include "canvas/embed/embedded_surface.h"

#include <unordered_set>
#include <utility>
#include <variant>

namespace canvas::embed {

void EmbeddedSurfaceManager::sync(
    const document::Document& document, core::Rect viewport,
    const std::optional<document::NodeId>& activeNode) {
  std::unordered_set<document::NodeId> required;

  for (const auto& node : document.nodes()) {
    if (node.layer != document::LayerClass::Embedded ||
        !node.bounds.intersects(viewport)) {
      continue;
    }

    const auto* embedded = std::get_if<document::EmbeddedNode>(&node.payload);
    if (embedded == nullptr) {
      continue;
    }

    const bool isActive = activeNode && *activeNode == node.id;
    const bool shouldLive =
        embedded->kind == document::EmbeddedKind::Video ||
        (embedded->kind == document::EmbeddedKind::Web && isActive);
    if (!shouldLive) {
      continue;
    }

    required.insert(node.id);
    auto live = live_.find(node.id);
    if (live == live_.end()) {
      auto surface = factory_.create(node);
      if (!surface) {
        // No surface is available yet. Keep the entry absent so a later sync
        // can retry without ever storing or dereferencing a null surface.
        continue;
      }
      live = live_.emplace(node.id, std::move(surface)).first;
    }

    live->second->setBounds(node.bounds);
    live->second->setInteractive(isActive);
    live->second->setVisible(true);
  }

  for (auto live = live_.begin(); live != live_.end();) {
    if (required.find(live->first) == required.end()) {
      live = live_.erase(live);
    } else {
      ++live;
    }
  }
}

}  // namespace canvas::embed
