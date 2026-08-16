#include "scene_compiler.h"

#include <algorithm>

namespace canvas::poc01 {

RuntimeScene SceneCompiler::Compile(const Document& document) const {
  const DocumentState& source = document.state();
  RuntimeScene scene;
  scene.source_revision = source.revision;
  scene.page_width = source.page_width;
  scene.page_height = source.page_height;
  scene.background = source.background;
  scene.draw_items.reserve(source.nodes.size());
  for (const auto& [id, node] : source.nodes) {
    static_cast<void>(id);
    scene.draw_items.push_back(node);
  }
  std::sort(scene.draw_items.begin(), scene.draw_items.end(),
            [](const Node& left, const Node& right) {
              const NodeHeader& lhs = Header(left);
              const NodeHeader& rhs = Header(right);
              return lhs.order == rhs.order ? lhs.id < rhs.id
                                            : lhs.order < rhs.order;
            });
  return scene;
}

}  // namespace canvas::poc01
