#include "canvas/stroke/stroke_builder.h"

namespace canvas::stroke {

StrokeBuilder::StrokeBuilder(float width) : width_(width) {
  committed_.reserve(512);
  predicted_.reserve(16);
}

document::StrokePoint StrokeBuilder::toPoint(
    const input::PointerSample& sample) const {
  return {sample.screenPosition, sample.pressure, sample.timestampMicros};
}

void StrokeBuilder::begin(const input::PointerSample& sample) {
  committed_.clear();
  predicted_.clear();
  committed_.push_back(toPoint(sample));
}

StrokeUpdate StrokeBuilder::append(const input::PointerSample& sample) {
  if (!sample.predicted) {
    predicted_.clear();
  }

  auto& target = sample.predicted ? predicted_ : committed_;
  const core::Vec2 previous = !target.empty()
                                  ? target.back().position
                                  : committed_.back().position;
  const core::Vec2 current = sample.screenPosition;
  const float dx = current.x - previous.x;
  const float dy = current.y - previous.y;
  if (dx * dx + dy * dy < 0.25F) {
    return {{}, false};
  }

  target.push_back(toPoint(sample));
  return {core::Rect::fromPoints(previous, current).inflated(width_), true};
}

document::StrokeNode StrokeBuilder::finish() {
  predicted_.clear();
  document::StrokeNode result;
  result.points = committed_;
  result.width = width_;
  return result;
}

}  // namespace canvas::stroke
