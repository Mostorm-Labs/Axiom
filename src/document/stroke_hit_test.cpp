#include "canvas/document/stroke_hit_test.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace canvas::document {

namespace {

struct Vec2d {
  double x = 0.0;
  double y = 0.0;
};

class WorkCounter {
 public:
  explicit WorkCounter(std::size_t limit) noexcept : limit_(limit) {}

  bool spend() noexcept {
    if (consumed_ >= limit_) return false;
    ++consumed_;
    return true;
  }

  std::size_t consumed() const noexcept { return consumed_; }

 private:
  std::size_t limit_ = 0;
  std::size_t consumed_ = 0;
};

enum class CandidateStatus { NoHit, Hit, BudgetExhausted };

struct CandidateResult {
  CandidateStatus status = CandidateStatus::NoHit;
};

bool finitePoint(core::Vec2 point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

bool finitePositiveBounds(const core::Rect& bounds) noexcept {
  return std::isfinite(bounds.x) && std::isfinite(bounds.y) &&
         std::isfinite(bounds.width) && std::isfinite(bounds.height) &&
         bounds.width > 0.0F && bounds.height > 0.0F;
}

Vec2d subtract(Vec2d left, Vec2d right) noexcept {
  return {left.x - right.x, left.y - right.y};
}

double dot(Vec2d left, Vec2d right) noexcept {
  return left.x * right.x + left.y * right.y;
}

double pointDistanceSquared(Vec2d left, Vec2d right) noexcept {
  const Vec2d delta = subtract(left, right);
  return dot(delta, delta);
}

struct ExactDifference {
  double high = 0.0;
  double low = 0.0;
};

struct FixedExpansion {
  static constexpr std::size_t kCapacity = 32;
  double components[kCapacity]{};
  std::size_t size = 0;
};

ExactDifference exactDifference(double left, double right) noexcept {
  const double high = left - right;
  const double rightVirtual = left - high;
  const double leftVirtual = high + rightVirtual;
  const double rightRoundoff = rightVirtual - right;
  const double leftRoundoff = left - leftVirtual;
  return {high, leftRoundoff + rightRoundoff};
}

void exactSum(double left, double right, double& high,
              double& low) noexcept {
  high = left + right;
  const double rightVirtual = high - left;
  const double leftVirtual = high - rightVirtual;
  const double rightRoundoff = right - rightVirtual;
  const double leftRoundoff = left - leftVirtual;
  low = leftRoundoff + rightRoundoff;
}

void appendExpansionTerm(FixedExpansion& expansion, double term) noexcept {
  if (term == 0.0) return;

  double output[FixedExpansion::kCapacity]{};
  std::size_t outputSize = 0;
  double accumulator = term;
  for (std::size_t index = 0; index < expansion.size; ++index) {
    double sum = 0.0;
    double roundoff = 0.0;
    exactSum(accumulator, expansion.components[index], sum, roundoff);
    if (roundoff != 0.0) output[outputSize++] = roundoff;
    accumulator = sum;
  }
  if (accumulator != 0.0 || outputSize == 0) {
    output[outputSize++] = accumulator;
  }
  for (std::size_t index = 0; index < outputSize; ++index) {
    expansion.components[index] = output[index];
  }
  expansion.size = outputSize;
}

void appendExactProduct(FixedExpansion& expansion, double left,
                        double right, double sign) noexcept {
  const double high = left * right;
  const double low = std::fma(left, right, -high);
  appendExpansionTerm(expansion, sign * low);
  appendExpansionTerm(expansion, sign * high);
}

void appendDifferenceProduct(FixedExpansion& expansion,
                             ExactDifference left,
                             ExactDifference right,
                             double sign) noexcept {
  appendExactProduct(expansion, left.low, right.low, sign);
  appendExactProduct(expansion, left.low, right.high, sign);
  appendExactProduct(expansion, left.high, right.low, sign);
  appendExactProduct(expansion, left.high, right.high, sign);
}

FixedExpansion exactDot(ExactDifference firstX,
                        ExactDifference firstY,
                        ExactDifference secondX,
                        ExactDifference secondY) noexcept {
  FixedExpansion result;
  appendDifferenceProduct(result, firstX, secondX, 1.0);
  appendDifferenceProduct(result, firstY, secondY, 1.0);
  return result;
}

FixedExpansion exactCross(ExactDifference firstX,
                          ExactDifference firstY,
                          ExactDifference secondX,
                          ExactDifference secondY) noexcept {
  FixedExpansion result;
  appendDifferenceProduct(result, firstX, secondY, 1.0);
  appendDifferenceProduct(result, firstY, secondX, -1.0);
  return result;
}

int expansionSign(const FixedExpansion& expansion) noexcept {
  for (std::size_t reverse = expansion.size; reverse > 0; --reverse) {
    const double component = expansion.components[reverse - 1U];
    if (component > 0.0) return 1;
    if (component < 0.0) return -1;
  }
  return 0;
}

double expansionEstimate(const FixedExpansion& expansion) noexcept {
  double estimate = 0.0;
  for (std::size_t index = 0; index < expansion.size; ++index) {
    estimate += expansion.components[index];
  }
  return estimate;
}

// Exact sign of the orientation predicate for the finite double coordinates
// used by this query. Fixed-size floating-point expansions retain subtraction
// and product roundoff, avoiding cancellation without heap allocation.
int orientationSign(Vec2d first, Vec2d second, Vec2d third) noexcept {
  const ExactDifference firstX = exactDifference(second.x, first.x);
  const ExactDifference firstY = exactDifference(second.y, first.y);
  const ExactDifference secondX = exactDifference(third.x, first.x);
  const ExactDifference secondY = exactDifference(third.y, first.y);
  return expansionSign(exactCross(firstX, firstY, secondX, secondY));
}

bool betweenInclusive(double value, double first, double second) noexcept {
  return value >= std::min(first, second) &&
         value <= std::max(first, second);
}

bool onSegment(Vec2d point, Vec2d start, Vec2d end) noexcept {
  return betweenInclusive(point.x, start.x, end.x) &&
         betweenInclusive(point.y, start.y, end.y);
}

// Projection signs choose the nearest endpoint without reconstructing a
// closest point. For an interior projection, |cross| / |segment| is the
// perpendicular distance. Exact fixed expansions retain small projections
// and cross products beside extreme finite float coordinates.
double pointSegmentDistanceSquared(Vec2d point, Vec2d start,
                                   Vec2d end) noexcept {
  const ExactDifference segmentX = exactDifference(end.x, start.x);
  const ExactDifference segmentY = exactDifference(end.y, start.y);
  const ExactDifference fromStartX = exactDifference(point.x, start.x);
  const ExactDifference fromStartY = exactDifference(point.y, start.y);
  const FixedExpansion startProjection =
      exactDot(fromStartX, fromStartY, segmentX, segmentY);
  if (expansionSign(startProjection) <= 0) {
    return pointDistanceSquared(point, start);
  }

  const ExactDifference fromEndX = exactDifference(point.x, end.x);
  const ExactDifference fromEndY = exactDifference(point.y, end.y);
  const FixedExpansion endProjection =
      exactDot(fromEndX, fromEndY, segmentX, segmentY);
  if (expansionSign(endProjection) >= 0) {
    return pointDistanceSquared(point, end);
  }

  const FixedExpansion determinant =
      exactCross(segmentX, segmentY, fromStartX, fromStartY);
  if (expansionSign(determinant) == 0) return 0.0;

  const Vec2d segment = subtract(end, start);
  const double length = std::hypot(segment.x, segment.y);
  if (!(length > 0.0) || !std::isfinite(length)) {
    return pointDistanceSquared(point, start);
  }
  const double distance = std::fabs(expansionEstimate(determinant)) / length;
  const double squared = distance * distance;
  return squared >= 0.0 && std::isfinite(squared)
             ? squared
             : std::numeric_limits<double>::infinity();
}

bool segmentsIntersect(Vec2d firstStart, Vec2d firstEnd,
                       Vec2d secondStart, Vec2d secondEnd) noexcept {
  const int firstToSecondStart =
      orientationSign(firstStart, firstEnd, secondStart);
  const int firstToSecondEnd =
      orientationSign(firstStart, firstEnd, secondEnd);
  const int secondToFirstStart =
      orientationSign(secondStart, secondEnd, firstStart);
  const int secondToFirstEnd =
      orientationSign(secondStart, secondEnd, firstEnd);

  const bool firstStraddles =
      (firstToSecondStart < 0 && firstToSecondEnd > 0) ||
      (firstToSecondStart > 0 && firstToSecondEnd < 0);
  const bool secondStraddles =
      (secondToFirstStart < 0 && secondToFirstEnd > 0) ||
      (secondToFirstStart > 0 && secondToFirstEnd < 0);
  if (firstStraddles && secondStraddles) return true;

  return (firstToSecondStart == 0 &&
          onSegment(secondStart, firstStart, firstEnd)) ||
         (firstToSecondEnd == 0 &&
          onSegment(secondEnd, firstStart, firstEnd)) ||
         (secondToFirstStart == 0 &&
          onSegment(firstStart, secondStart, secondEnd)) ||
         (secondToFirstEnd == 0 &&
          onSegment(firstEnd, secondStart, secondEnd));
}

// Intersecting closed segments have exactly zero distance under the robust
// orientation predicate. For two disjoint 2D segments, the minimum distance
// is attained by an endpoint of one segment against the other segment.
double segmentDistanceSquared(Vec2d firstStart, Vec2d firstEnd,
                              Vec2d secondStart, Vec2d secondEnd) noexcept {
  const Vec2d first = subtract(firstEnd, firstStart);
  const Vec2d second = subtract(secondEnd, secondStart);
  const double firstLengthSquared = dot(first, first);
  const double secondLengthSquared = dot(second, second);
  if (!(firstLengthSquared > 0.0) ||
      !std::isfinite(firstLengthSquared)) {
    return pointSegmentDistanceSquared(firstStart, secondStart, secondEnd);
  }
  if (!(secondLengthSquared > 0.0) ||
      !std::isfinite(secondLengthSquared)) {
    return pointSegmentDistanceSquared(secondStart, firstStart, firstEnd);
  }
  if (segmentsIntersect(firstStart, firstEnd, secondStart, secondEnd)) {
    return 0.0;
  }

  double squared =
      pointSegmentDistanceSquared(firstStart, secondStart, secondEnd);
  squared = std::min(
      squared,
      pointSegmentDistanceSquared(firstEnd, secondStart, secondEnd));
  squared = std::min(
      squared,
      pointSegmentDistanceSquared(secondStart, firstStart, firstEnd));
  squared = std::min(
      squared,
      pointSegmentDistanceSquared(secondEnd, firstStart, firstEnd));
  return squared >= 0.0 && std::isfinite(squared)
             ? squared
             : std::numeric_limits<double>::infinity();
}

bool worldPoint(const StrokePoint& point, const core::Rect* parent,
                Vec2d& mapped) noexcept {
  if (!finitePoint(point.position)) return false;
  if (parent == nullptr) {
    mapped = {static_cast<double>(point.position.x),
              static_cast<double>(point.position.y)};
    return true;
  }

  const core::Vec2 renderedPoint{
      parent->x + point.position.x * parent->width,
      parent->y + point.position.y * parent->height};
  if (!finitePoint(renderedPoint)) return false;
  mapped = {static_cast<double>(renderedPoint.x),
            static_cast<double>(renderedPoint.y)};
  return true;
}

bool broadPhaseIntersects(const core::Rect& bounds,
                          const StrokeSweepQuery& query,
                          double capsuleRadius) noexcept {
  const double sweepLeft =
      std::min(static_cast<double>(query.from.x),
               static_cast<double>(query.to.x));
  const double sweepRight =
      std::max(static_cast<double>(query.from.x),
               static_cast<double>(query.to.x));
  const double sweepTop =
      std::min(static_cast<double>(query.from.y),
               static_cast<double>(query.to.y));
  const double sweepBottom =
      std::max(static_cast<double>(query.from.y),
               static_cast<double>(query.to.y));
  const double nodeLeft = static_cast<double>(bounds.x) - capsuleRadius;
  const double nodeRight = static_cast<double>(bounds.x) +
                           static_cast<double>(bounds.width) + capsuleRadius;
  const double nodeTop = static_cast<double>(bounds.y) - capsuleRadius;
  const double nodeBottom = static_cast<double>(bounds.y) +
                            static_cast<double>(bounds.height) + capsuleRadius;
  return sweepRight >= nodeLeft && sweepLeft <= nodeRight &&
         sweepBottom >= nodeTop && sweepTop <= nodeBottom;
}

const Node* countedParentLookup(const Document& document,
                                const NodeId& parentId,
                                WorkCounter& work,
                                bool& exhausted) noexcept {
  for (const auto& candidate : document.nodes()) {
    if (!work.spend()) {
      exhausted = true;
      return nullptr;
    }
    if (candidate.id == parentId) return &candidate;
  }
  return nullptr;
}

CandidateResult evaluateStroke(const StrokeNode& stroke,
                               const core::Rect* parent,
                               const StrokeSweepQuery& query,
                               WorkCounter& work) noexcept {
  if (!std::isfinite(stroke.width) || !(stroke.width > 0.0F) ||
      stroke.points.empty()) {
    return {};
  }

  float renderedWidth = stroke.width;
  if (parent != nullptr) {
    const float minimumExtent = std::min(parent->width, parent->height);
    renderedWidth = stroke.width * minimumExtent;
  }
  if (!std::isfinite(renderedWidth) || renderedWidth < 0.0F) {
    return {};
  }
  const double worldWidth = static_cast<double>(renderedWidth);

  const double capsuleRadius = static_cast<double>(query.eraserRadius) +
                               worldWidth * 0.5;
  const double thresholdSquared = capsuleRadius * capsuleRadius;
  if (!std::isfinite(capsuleRadius) ||
      !std::isfinite(thresholdSquared)) {
    return {};
  }

  const Vec2d sweepStart{static_cast<double>(query.from.x),
                         static_cast<double>(query.from.y)};
  const Vec2d sweepEnd{static_cast<double>(query.to.x),
                       static_cast<double>(query.to.y)};
  Vec2d previous;
  if (stroke.points.size() == 1U) {
    if (!work.spend()) return {CandidateStatus::BudgetExhausted};
    if (!worldPoint(stroke.points.front(), parent, previous)) return {};
    const double squared =
        pointSegmentDistanceSquared(previous, sweepStart, sweepEnd);
    return {squared <= thresholdSquared ? CandidateStatus::Hit
                                        : CandidateStatus::NoHit};
  }

  bool hit = false;
  for (std::size_t index = 1; index < stroke.points.size(); ++index) {
    if (!work.spend()) return {CandidateStatus::BudgetExhausted};
    if (index == 1U &&
        !worldPoint(stroke.points.front(), parent, previous)) {
      return {};
    }
    Vec2d current;
    if (!worldPoint(stroke.points[index], parent, current)) return {};
    const double squared =
        segmentDistanceSquared(previous, current, sweepStart, sweepEnd);
    if (squared <= thresholdSquared) hit = true;
    previous = current;
  }
  return {hit ? CandidateStatus::Hit : CandidateStatus::NoHit};
}

StrokeHitTestResult exhaustedResult(const WorkCounter& work) noexcept {
  StrokeHitTestResult result;
  result.status = StrokeHitTestStatus::BudgetExhausted;
  result.workConsumed = work.consumed();
  return result;
}

StrokeHitTestResult scanLayer(const Document& document,
                              const StrokeSweepQuery& query,
                              LayerClass layer,
                              WorkCounter& work) noexcept {
  const auto& nodes = document.nodes();
  for (std::size_t reverse = nodes.size(); reverse > 0; --reverse) {
    if (!work.spend()) return exhaustedResult(work);
    const std::size_t index = reverse - 1U;
    const Node& node = nodes[index];
    if (node.layer != layer || node.cacheIdentity == 0 ||
        !finitePositiveBounds(node.bounds)) {
      continue;
    }
    const auto* stroke = std::get_if<StrokeNode>(&node.payload);
    if (stroke == nullptr) continue;

    const core::Rect* parentBounds = nullptr;
    if (layer == LayerClass::Annotation) {
      if (stroke->coordinateSpace !=
              StrokeCoordinateSpace::ParentNormalized ||
          !node.parentId) {
        continue;
      }
      bool parentLookupExhausted = false;
      const Node* parent = countedParentLookup(
          document, *node.parentId, work, parentLookupExhausted);
      if (parentLookupExhausted) return exhaustedResult(work);
      if (parent == nullptr || parent->layer != LayerClass::Embedded ||
          !std::holds_alternative<EmbeddedNode>(parent->payload) ||
          !finitePositiveBounds(parent->bounds)) {
        continue;
      }
      parentBounds = &parent->bounds;
    } else {
      if (stroke->coordinateSpace != StrokeCoordinateSpace::World ||
          !std::isfinite(stroke->width) || !(stroke->width > 0.0F)) {
        continue;
      }
      const double capsuleRadius =
          static_cast<double>(query.eraserRadius) +
          static_cast<double>(stroke->width) * 0.5;
      if (!std::isfinite(capsuleRadius) ||
          !broadPhaseIntersects(node.bounds, query, capsuleRadius)) {
        continue;
      }
    }

    const CandidateResult candidate =
        evaluateStroke(*stroke, parentBounds, query, work);
    if (candidate.status == CandidateStatus::BudgetExhausted) {
      return exhaustedResult(work);
    }
    if (candidate.status == CandidateStatus::Hit) {
      StrokeHitTestResult result;
      result.status = StrokeHitTestStatus::Hit;
      result.token = StrokeHitToken{index, node.cacheIdentity, layer};
      result.workConsumed = work.consumed();
      return result;
    }
  }

  StrokeHitTestResult result;
  result.workConsumed = work.consumed();
  return result;
}

}  // namespace

StrokeHitTestResult findTopmostStrokeHit(
    const Document& document, const StrokeSweepQuery& query) noexcept {
  if (!finitePoint(query.from) || !finitePoint(query.to) ||
      !std::isfinite(query.eraserRadius) || query.eraserRadius < 0.0F) {
    StrokeHitTestResult invalid;
    invalid.status = StrokeHitTestStatus::InvalidQuery;
    return invalid;
  }

  WorkCounter work(query.workBudget);

  StrokeHitTestResult result =
      scanLayer(document, query, LayerClass::Annotation, work);
  if (result.status != StrokeHitTestStatus::NoHit) return result;
  return scanLayer(document, query, LayerClass::Base, work);
}

}  // namespace canvas::document
