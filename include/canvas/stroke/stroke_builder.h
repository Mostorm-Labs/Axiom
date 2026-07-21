#pragma once

#include "canvas/document/node.h"
#include "canvas/input/pointer_sample.h"

#include <vector>

namespace canvas::stroke {

struct StrokeUpdate {
  // `accepted` is independent of invalidation: a filtered real sample can
  // report false while dirtyBounds is non-empty and still must be invalidated.
  core::Rect dirtyBounds;
  bool accepted = false;
};

class StrokeBuilder {
 public:
  explicit StrokeBuilder(float width);

  void begin(const input::PointerSample& sample);
  StrokeUpdate append(const input::PointerSample& sample);
  document::StrokeNode finish();
  // Returns the predicted preview area invalidated by the last finish();
  // begin() resets it and callers should read it after finish().
  core::Rect finishDirtyBounds() const noexcept;

 private:
  document::StrokePoint toPoint(const input::PointerSample& sample) const;
  core::Rect predictedBounds() const;

  float width_;
  std::vector<document::StrokePoint> committed_;
  std::vector<document::StrokePoint> predicted_;
  core::Rect finishDirtyBounds_{};
};

}  // namespace canvas::stroke
