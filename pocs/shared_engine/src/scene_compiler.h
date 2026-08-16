#ifndef CANVAS_POC_SCENE_COMPILER_H_
#define CANVAS_POC_SCENE_COMPILER_H_

#include <cstdint>
#include <vector>

#include "document.h"

namespace canvas::poc01 {

struct RuntimeScene {
  uint64_t source_revision = 0;
  uint32_t page_width = 0;
  uint32_t page_height = 0;
  Color background;
  std::vector<Node> draw_items;
};

class SceneCompiler {
 public:
  // POC-01 deliberately performs a complete, reproducible rebuild. Spatial
  // indexing and incremental compilation belong to POC-03.
  [[nodiscard]] RuntimeScene Compile(const Document& document) const;
};

}  // namespace canvas::poc01

#endif  // CANVAS_POC_SCENE_COMPILER_H_
