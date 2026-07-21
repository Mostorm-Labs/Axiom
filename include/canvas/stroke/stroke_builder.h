#pragma once

#include "canvas/document/node.h"
#include "canvas/input/pointer_sample.h"

#include <vector>

namespace canvas::stroke {

struct StrokeUpdate {
  core::Rect dirtyBounds;
  bool accepted = false;
};

class StrokeBuilder {
 public:
  explicit StrokeBuilder(float width);

  void begin(const input::PointerSample& sample);
  StrokeUpdate append(const input::PointerSample& sample);
  document::StrokeNode finish();

 private:
  document::StrokePoint toPoint(const input::PointerSample& sample) const;

  float width_;
  std::vector<document::StrokePoint> committed_;
  std::vector<document::StrokePoint> predicted_;
};

}  // namespace canvas::stroke
