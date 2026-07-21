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

core::Rect StrokeBuilder::predictedBounds() const {
  core::Rect bounds = core::Rect::fromPoints(
      committed_.back().position, predicted_.front().position);
  for (std::size_t index = 1; index < predicted_.size(); ++index) {
    bounds = bounds.united(core::Rect::fromPoints(
        predicted_[index - 1].position, predicted_[index].position));
  }
  return bounds.inflated(width_);
}

core::Rect StrokeBuilder::finishDirtyBounds() const noexcept {
  return finishDirtyBounds_;
}

void StrokeBuilder::begin(const input::PointerSample& sample) {
  committed_.clear();
  predicted_.clear();
  finishDirtyBounds_ = {};
  committed_.push_back(toPoint(sample));
}

StrokeUpdate StrokeBuilder::append(const input::PointerSample& sample) {
  const bool replacingPredictedTail = !sample.predicted && !predicted_.empty();
  const core::Rect replacedPredictedBounds =
      replacingPredictedTail ? predictedBounds() : core::Rect{};

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
    return {replacedPredictedBounds, false};
  }

  target.push_back(toPoint(sample));
  core::Rect dirtyBounds =
      core::Rect::fromPoints(previous, current).inflated(width_);
  if (replacingPredictedTail) {
    dirtyBounds = replacedPredictedBounds.united(dirtyBounds);
  }
  return {dirtyBounds, true};
}

document::StrokeNode StrokeBuilder::finish() {
  finishDirtyBounds_ =
      predicted_.empty() ? core::Rect{} : predictedBounds();
  predicted_.clear();
  document::StrokeNode result;
  result.points = committed_;
  result.width = width_;
  return result;
}

}  // namespace canvas::stroke
