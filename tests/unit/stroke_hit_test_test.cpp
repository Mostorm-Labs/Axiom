#include "canvas/document/stroke_hit_test.h"

#include <gtest/gtest.h>

#include <cfloat>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace canvas::document {
namespace {

constexpr std::size_t kAmpleBudget = 10'000;

static_assert(noexcept(findTopmostStrokeHit(
    std::declval<const Document&>(),
    std::declval<const StrokeSweepQuery&>())));

Node worldStroke(std::string id, LayerClass layer,
                 std::vector<core::Vec2> positions, float width,
                 core::Rect bounds) {
  StrokeNode stroke;
  stroke.width = width;
  stroke.coordinateSpace = StrokeCoordinateSpace::World;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    stroke.points.push_back(
        {positions[index], 0.5F, static_cast<std::uint64_t>(index + 1)});
  }
  return {std::move(id), layer, bounds, {}, std::move(stroke)};
}

Node attachedStroke(std::string id, std::string parentId,
                    std::vector<core::Vec2> positions, float relativeWidth,
                    core::Rect staleBounds = {1, 1, 1, 1}) {
  StrokeNode stroke;
  stroke.width = relativeWidth;
  stroke.coordinateSpace = StrokeCoordinateSpace::ParentNormalized;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    stroke.points.push_back(
        {positions[index], 0.5F, static_cast<std::uint64_t>(index + 1)});
  }
  return {std::move(id), LayerClass::Annotation, staleBounds,
          std::move(parentId), std::move(stroke)};
}

Node embedded(std::string id, core::Rect bounds) {
  return {std::move(id), LayerClass::Embedded, bounds, {},
          EmbeddedNode{EmbeddedKind::Web, "https://example.com", "web"}};
}

StrokeSweepQuery sweep(core::Vec2 from, core::Vec2 to, float radius,
                       std::size_t budget = kAmpleBudget) {
  return {from, to, radius, budget};
}

const StrokeHitToken& requireHit(const StrokeHitTestResult& result) {
  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit);
  if (!result.token) {
    ADD_FAILURE() << "hit result must carry a stable token";
    static constexpr StrokeHitToken missing{};
    return missing;
  }
  return *result.token;
}

TEST(StrokeHitTest, AnnotationLayerAndReverseZOrderPrecedeBase) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "base-low", LayerClass::Base, {{0, 50}, {100, 50}}, 4,
      {0, 48, 100, 4})));
  ASSERT_TRUE(document.add(embedded("web", {0, 0, 100, 100})));
  ASSERT_TRUE(document.add(attachedStroke(
      "annotation-low", "web", {{0, 0.5F}, {1, 0.5F}}, 0.04F)));
  ASSERT_TRUE(document.add(worldStroke(
      "base-high", LayerClass::Base, {{0, 50}, {100, 50}}, 4,
      {0, 48, 100, 4})));
  ASSERT_TRUE(document.add(attachedStroke(
      "annotation-high", "web", {{0, 0.5F}, {1, 0.5F}}, 0.04F)));

  const auto result = findTopmostStrokeHit(
      document, sweep({50, 40}, {50, 60}, 1));

  const auto& token = requireHit(result);
  EXPECT_EQ(token.nodeIndex, 4U);
  EXPECT_EQ(token.cacheIdentity, document.nodes()[4].cacheIdentity);
  EXPECT_EQ(token.layer, LayerClass::Annotation);
  EXPECT_LE(result.workConsumed, kAmpleBudget);
}

TEST(StrokeHitTest, BaseUsesReverseZOrderAndReturnsStableIdentityToken) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "first", LayerClass::Base, {{0, 10}, {20, 10}}, 2,
      {0, 9, 20, 2})));
  ASSERT_TRUE(document.add(worldStroke(
      "second", LayerClass::Base, {{0, 10}, {20, 10}}, 2,
      {0, 9, 20, 2})));

  const auto result = findTopmostStrokeHit(
      document, sweep({10, 0}, {10, 20}, 1));

  const auto& token = requireHit(result);
  EXPECT_EQ(token, (StrokeHitToken{1, document.nodes()[1].cacheIdentity,
                                   LayerClass::Base}));
}

TEST(StrokeHitTest, SparseSweepHitsWorldSegmentBetweenEventPositions) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "vertical", LayerClass::Base, {{50, -10}, {50, 10}}, 2,
      {49, -10, 2, 20})));

  const auto result = findTopmostStrokeHit(
      document, sweep({0, 0}, {100, 0}, 1));

  EXPECT_EQ(requireHit(result).nodeIndex, 0U);
}

TEST(StrokeHitTest, SinglePointAndTrueHalfWidthParticipateInCapsuleHit) {
  Document singlePoint;
  ASSERT_TRUE(singlePoint.add(worldStroke(
      "dot", LayerClass::Base, {{20, 20}}, 6, {17, 17, 6, 6})));
  EXPECT_EQ(findTopmostStrokeHit(
                singlePoint, sweep({24, 20}, {24, 20}, 1))
                .status,
            StrokeHitTestStatus::Hit);

  Document duplicatePointSegment;
  ASSERT_TRUE(duplicatePointSegment.add(worldStroke(
      "duplicate", LayerClass::Base, {{20, 20}, {20, 20}}, 6,
      {17, 17, 6, 6})));
  EXPECT_EQ(findTopmostStrokeHit(
                duplicatePointSegment,
                sweep({24, 20}, {24, 20}, 1))
                .status,
            StrokeHitTestStatus::Hit)
      << "both a degenerate sweep and a degenerate stroke segment are safe";

  Document thickLine;
  ASSERT_TRUE(thickLine.add(worldStroke(
      "thick", LayerClass::Base, {{0, 0}, {20, 0}}, 10,
      {0, -5, 20, 10})));
  EXPECT_EQ(findTopmostStrokeHit(
                thickLine, sweep({10, 7}, {10, 7}, 2))
                .status,
            StrokeHitTestStatus::Hit)
      << "eraser radius 2 plus true half-width 5 includes the boundary";
  EXPECT_EQ(findTopmostStrokeHit(
                thickLine, sweep({10, 7.25F}, {10, 7.25F}, 2))
                .status,
            StrokeHitTestStatus::NoHit);
}

TEST(StrokeHitTest,
     ParentNormalizedUsesCurrentMovedAnisotropicParentAndResolvedWidth) {
  Document document;
  ASSERT_TRUE(document.add(embedded("web", {100, 100, 200, 100})));
  ASSERT_TRUE(document.add(attachedStroke(
      "annotation", "web", {{0, 0.5F}, {1, 0.5F}}, 0.1F,
      {100, 145, 200, 10})));
  ASSERT_TRUE(document.setBounds("web", {300, 200, 400, 50}));

  const auto centerHit = findTopmostStrokeHit(
      document, sweep({500, 210}, {500, 240}, 1));
  EXPECT_EQ(requireHit(centerHit).nodeIndex, 1U);

  const auto widthHit = findTopmostStrokeHit(
      document, sweep({500, 228.5F}, {500, 228.5F}, 1));
  EXPECT_EQ(widthHit.status, StrokeHitTestStatus::Hit)
      << "relative width 0.1 resolves against min(400, 50) to 5 points";
  const auto outsideWidth = findTopmostStrokeHit(
      document, sweep({500, 228.75F}, {500, 228.75F}, 1));
  EXPECT_EQ(outsideWidth.status, StrokeHitTestStatus::NoHit);
}

TEST(StrokeHitTest,
     ParentNormalizedPointOutsideParentIgnoresStaleChildAndParentBounds) {
  Document document;
  ASSERT_TRUE(document.add(embedded("web", {10, 10, 100, 100})));
  ASSERT_TRUE(document.add(attachedStroke(
      "outside", "web", {{1.5F, 0.5F}}, 0.02F,
      {10, 10, 1, 1})));

  const auto result = findTopmostStrokeHit(
      document, sweep({160, 60}, {160, 60}, 0));

  EXPECT_EQ(requireHit(result).nodeIndex, 1U);
}

TEST(StrokeHitTest, EmbeddedChromeAndNonStrokePayloadsAreNeverTargets) {
  Document document;
  ASSERT_TRUE(document.add(embedded("video", {0, 0, 100, 100})));
  ASSERT_TRUE(document.add(worldStroke(
      "embedded-stroke", LayerClass::Embedded, {{0, 50}, {100, 50}}, 10,
      {0, 45, 100, 10})));
  ASSERT_TRUE(document.add(worldStroke(
      "chrome-stroke", LayerClass::Chrome, {{0, 50}, {100, 50}}, 10,
      {0, 45, 100, 10})));
  Node embeddedPayloadOnAnnotation{
      "wrong-payload", LayerClass::Annotation, {0, 0, 100, 100}, {},
      EmbeddedNode{EmbeddedKind::Video, "video.mp4", "video"}};
  ASSERT_TRUE(document.add(std::move(embeddedPayloadOnAnnotation)));
  Node unknown{"unknown", LayerClass::Base, {0, 0, 100, 100}, {},
               UnknownNode{"future", "{}"}};
  ASSERT_TRUE(document.add(std::move(unknown)));

  const auto result = findTopmostStrokeHit(
      document, sweep({50, 40}, {50, 60}, 10));

  EXPECT_EQ(result.status, StrokeHitTestStatus::NoHit);
  EXPECT_FALSE(result.token.has_value());
}

TEST(StrokeHitTest, InvalidQueryIsExplicitAndConsumesNoWork) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "line", LayerClass::Base, {{0, 0}, {10, 0}}, 2,
      {0, -1, 10, 2})));
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();

  for (const auto& query : {
           sweep({nan, 0}, {0, 0}, 1),
           sweep({0, 0}, {infinity, 0}, 1),
           sweep({0, 0}, {0, 0}, -1),
           sweep({0, 0}, {0, 0}, nan),
       }) {
    const auto result = findTopmostStrokeHit(document, query);
    EXPECT_EQ(result.status, StrokeHitTestStatus::InvalidQuery);
    EXPECT_FALSE(result.token.has_value());
    EXPECT_EQ(result.workConsumed, 0U);
  }
}

TEST(StrokeHitTest, InvalidStrokeGeometryAndMissingParentFailClosed) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  Document invalidWorld;
  ASSERT_TRUE(invalidWorld.add(worldStroke(
      "nan-point", LayerClass::Base, {{0, 0}, {nan, 0}}, 2,
      {0, -1, 10, 2})));
  EXPECT_EQ(findTopmostStrokeHit(
                invalidWorld, sweep({0, -5}, {0, 5}, 2))
                .status,
            StrokeHitTestStatus::NoHit);

  Document zeroWidth;
  ASSERT_TRUE(zeroWidth.add(worldStroke(
      "zero-width", LayerClass::Base, {{0, 0}, {10, 0}}, 0,
      {0, -1, 10, 2})));
  EXPECT_EQ(findTopmostStrokeHit(
                zeroWidth, sweep({0, -5}, {0, 5}, 2))
                .status,
            StrokeHitTestStatus::NoHit);

  Document invalidBounds;
  ASSERT_TRUE(invalidBounds.add(worldStroke(
      "bad-bounds", LayerClass::Base, {{0, 0}, {10, 0}}, 2,
      {0, 0, 0, 2})));
  EXPECT_EQ(findTopmostStrokeHit(
                invalidBounds, sweep({0, -5}, {0, 5}, 2))
                .status,
            StrokeHitTestStatus::NoHit);

  Document missingParent;
  ASSERT_TRUE(missingParent.add(embedded("web", {0, 0, 100, 100})));
  ASSERT_TRUE(missingParent.add(attachedStroke(
      "orphan", "web", {{0, 0.5F}, {1, 0.5F}}, 0.02F)));
  ASSERT_TRUE(missingParent.mutate("orphan", [](Node& node) {
    node.parentId = "missing";
  }));
  EXPECT_EQ(findTopmostStrokeHit(
                missingParent, sweep({50, 40}, {50, 60}, 2))
                .status,
            StrokeHitTestStatus::NoHit);
}

TEST(StrokeHitTest, OverflowingMappedGeometryFailsClosedWithoutException) {
  Document document;
  ASSERT_TRUE(document.add(embedded("web", {0, 0, FLT_MAX, 1})));
  ASSERT_TRUE(document.add(attachedStroke(
      "overflow", "web", {{FLT_MAX, 0.5F}}, 1.0F)));

  const auto result = findTopmostStrokeHit(
      document, sweep({0, 0}, {10, 0}, 1));

  EXPECT_EQ(result.status, StrokeHitTestStatus::NoHit);
  EXPECT_FALSE(result.token.has_value());
}

TEST(StrokeHitTest, HugeFiniteSweepDoesNotOverflowDistanceMath) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "vertical", LayerClass::Base, {{0, -10}, {0, 10}}, 2,
      {-1, -10, 2, 20})));

  const auto result = findTopmostStrokeHit(
      document, sweep({-FLT_MAX, 0}, {FLT_MAX, 0}, 1));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit);
}

TEST(StrokeHitTest, TinyPerpendicularSegmentsDoNotBecomeFalselyParallel) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "tiny", LayerClass::Base, {{0, 0}, {0.0001F, 0}}, 0.00000001F,
      {0, -0.000000005F, 0.0001F, 0.00000001F})));

  const auto result = findTopmostStrokeHit(
      document,
      sweep({0.00005F, -0.00005F}, {0.00005F, 0.00005F}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit);
}

TEST(StrokeHitTest, LongShallowCrossingSegmentsRemainAHit) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "long-shallow", LayerClass::Base,
      {{-1.0e8F, 0}, {1.0e8F, 0}}, 0.1F,
      {-1.0e8F, -0.05F, 2.0e8F, 0.1F})));

  const auto result = findTopmostStrokeHit(
      document,
      sweep({-1.0e8F, -1}, {1.0e8F, 1}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit);
}

TEST(StrokeHitTest, ExtremeInteriorCrossingIsNotClampedToAnEndpoint) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "extreme", LayerClass::Base, {{0, 0}, {FLT_MAX, 0}}, 0.1F,
      {0, -0.05F, FLT_MAX, 0.1F})));

  const auto result = findTopmostStrokeHit(
      document,
      sweep({1.0e20F, -1}, {1.0e20F, 1}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit);
}

TEST(StrokeHitTest, LargeGeneralSkewIntersectionRemainsAHit) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "large-skew", LayerClass::Base,
      {{-7.127652736583493e19F, 3.42760281497309e19F},
       {3.4706412185214517e19F, 6.908028807372446e19F}},
      2.0F, {-8.0e19F, 3.0e19F, 1.2e20F, 4.5e19F})));

  const auto result = findTopmostStrokeHit(
      document,
      sweep({2.3694647102386733e19F, 4.304328977803536e19F},
            {-7.126193024946458e19F, 7.621600858682792e19F}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit);
}

TEST(StrokeHitTest, ExtremeSinglePointOnSweepHasExactlyZeroDistance) {
  constexpr float start = 3.372519905416452e22F;
  constexpr float end = 2.842269790733038e38F;
  constexpr float point = 1.5210435097729066e38F;
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "extreme-point", LayerClass::Base, {{point, 0}}, 2,
      {0, -1, FLT_MAX, 2})));

  const auto result = findTopmostStrokeHit(
      document, sweep({start, 0}, {end, 0}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit);
}

TEST(StrokeHitTest,
     ExtremeDegenerateSweepOnStrokeHasZeroDistanceButOutsideDoesNot) {
  constexpr float start = 3.372519905416452e22F;
  constexpr float end = 2.842269790733038e38F;
  constexpr float point = 1.5210435097729066e38F;
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "extreme-line", LayerClass::Base, {{start, 0}, {end, 0}}, 2,
      {0, -1, FLT_MAX, 2})));

  const auto onSegmentResult = findTopmostStrokeHit(
      document, sweep({point, 0}, {point, 0}, 0));
  EXPECT_EQ(onSegmentResult.status, StrokeHitTestStatus::Hit);

  const auto outsideResult = findTopmostStrokeHit(
      document, sweep({FLT_MAX, 0}, {FLT_MAX, 0}, 0));
  EXPECT_EQ(outsideResult.status, StrokeHitTestStatus::NoHit)
      << "exact collinearity alone cannot extend a closed segment";
}

TEST(StrokeHitTest, ExtremeParallelCapsuleBoundaryRemainsInclusive) {
  constexpr float start = 3.372519905416452e22F;
  constexpr float end = 2.842269790733038e38F;
  constexpr float sweepStart = 4.526671604290312e24F;
  constexpr float sweepEnd = 9.378274534988486e24F;
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "extreme-parallel", LayerClass::Base, {{start, 0}, {end, 0}}, 2,
      {0, -1, FLT_MAX, 2})));

  const auto result = findTopmostStrokeHit(
      document, sweep({sweepStart, 1}, {sweepEnd, 1}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit)
      << "the exact one-unit capsule boundary is inclusive";
}

TEST(StrokeHitTest,
     ParentNormalizedLargeOriginMatchesRenderedFloatCoordinates) {
  Document document;
  ASSERT_TRUE(document.add(embedded("web", {1.0e8F, 0, 1, 1})));
  ASSERT_TRUE(document.add(attachedStroke(
      "outside", "web", {{4, 0.5F}}, 0.1F)));

  const auto result = findTopmostStrokeHit(
      document, sweep({1.0e8F, 0.5F}, {1.0e8F, 0.5F}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit)
      << "hit geometry must use the same float world point as rendering";
}

TEST(StrokeHitTest,
     ParentNormalizedWidthMatchesFiniteRendererFloatRoundingAtMaximum) {
  constexpr float kRoundingFactor = 1.0034993886947632F;
  constexpr float kLargeExtent = 3.390957242646672e38F;
  static_assert(kRoundingFactor * kLargeExtent == FLT_MAX);

  Document document;
  ASSERT_TRUE(document.add(
      embedded("web", {0, 0, kLargeExtent, kLargeExtent})));
  ASSERT_TRUE(document.add(attachedStroke(
      "maximum-width", "web", {{0, 0}}, kRoundingFactor)));

  const auto result = findTopmostStrokeHit(
      document, sweep({0, 0}, {0, 0}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit)
      << "hit width must accept the renderer's finite float result";
}

TEST(StrokeHitTest,
     ParentNormalizedPointMatchesFiniteRendererFloatRoundingAtMaximum) {
  constexpr float kRoundingFactor = 1.0034993886947632F;
  constexpr float kLargeExtent = 3.390957242646672e38F;
  static_assert(kRoundingFactor * kLargeExtent == FLT_MAX);

  Document document;
  ASSERT_TRUE(document.add(
      embedded("web", {0, 0, kLargeExtent, kLargeExtent})));
  ASSERT_TRUE(document.add(attachedStroke(
      "maximum-point", "web", {{kRoundingFactor, 0}}, 1.0e-38F)));

  const auto result = findTopmostStrokeHit(
      document, sweep({FLT_MAX, 0}, {FLT_MAX, 0}, 0));

  EXPECT_EQ(result.status, StrokeHitTestStatus::Hit)
      << "hit point must accept the renderer's finite float result";
}

TEST(StrokeHitTest,
     ParentNormalizedWidthUnderflowMatchesRendererHairlineCenterline) {
  constexpr float kSmallExtent =
      std::numeric_limits<float>::denorm_min();
  static_assert(kSmallExtent > 0.0F);
  static_assert(0.25F * kSmallExtent == 0.0F);

  Document document;
  ASSERT_TRUE(document.add(
      embedded("web", {0, 0, kSmallExtent, kSmallExtent})));
  ASSERT_TRUE(document.add(attachedStroke(
      "hairline", "web", {{0, 0}}, 0.25F)));

  const auto exactCenter = findTopmostStrokeHit(
      document, sweep({0, 0}, {0, 0}, 0));
  EXPECT_EQ(exactCenter.status, StrokeHitTestStatus::Hit)
      << "a finite renderer hairline remains erasable at its centerline";

  const auto eraserBoundary = findTopmostStrokeHit(
      document, sweep({1, 0}, {1, 0}, 1));
  EXPECT_EQ(eraserBoundary.status, StrokeHitTestStatus::Hit);

  const auto outsideEraser = findTopmostStrokeHit(
      document, sweep({1.25F, 0}, {1.25F, 0}, 1));
  EXPECT_EQ(outsideEraser.status, StrokeHitTestStatus::NoHit);
}

TEST(StrokeHitTest, ZeroBudgetDistinguishesEmptyFromUninspectableDocument) {
  Document empty;
  const auto emptyResult = findTopmostStrokeHit(
      empty, sweep({0, 0}, {0, 0}, 0, 0));
  EXPECT_EQ(emptyResult.status, StrokeHitTestStatus::NoHit);
  EXPECT_EQ(emptyResult.workConsumed, 0U);

  Document nonempty;
  ASSERT_TRUE(nonempty.add(worldStroke(
      "line", LayerClass::Base, {{0, 0}, {10, 0}}, 2,
      {0, -1, 10, 2})));
  const auto nonemptyResult = findTopmostStrokeHit(
      nonempty, sweep({5, -5}, {5, 5}, 0, 0));
  EXPECT_EQ(nonemptyResult.status, StrokeHitTestStatus::BudgetExhausted);
  EXPECT_EQ(nonemptyResult.workConsumed, 0U);
}

TEST(StrokeHitTest, NodeTraversalBudgetExhaustionIsExplicitAndFailClosed) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "base", LayerClass::Base, {{0, 0}, {10, 0}}, 2,
      {0, -1, 10, 2})));

  const auto result = findTopmostStrokeHit(
      document, sweep({5, -5}, {5, 5}, 1, 1));

  EXPECT_EQ(result.status, StrokeHitTestStatus::BudgetExhausted);
  EXPECT_FALSE(result.token.has_value());
  EXPECT_EQ(result.workConsumed, 1U);
}

TEST(StrokeHitTest, ParentLookupBudgetIsCountedAndBounded) {
  Node child = attachedStroke(
      "child", "parent", {{0, 0.5F}, {1, 0.5F}}, 0.02F);
  Node parent = embedded("parent", {0, 0, 100, 100});
  Document document;
  ASSERT_TRUE(document.replaceValidatedNodes({std::move(child),
                                               std::move(parent)}));

  const auto result = findTopmostStrokeHit(
      document, sweep({50, 40}, {50, 60}, 1, 3));

  EXPECT_EQ(result.status, StrokeHitTestStatus::BudgetExhausted);
  EXPECT_FALSE(result.token.has_value());
  EXPECT_EQ(result.workConsumed, 3U)
      << "the first parent candidate consumes work before the second can run";

  const auto exactBudgetHit = findTopmostStrokeHit(
      document, sweep({50, 40}, {50, 60}, 1, 5));
  EXPECT_EQ(exactBudgetHit.status, StrokeHitTestStatus::Hit);
  EXPECT_EQ(exactBudgetHit.workConsumed, 5U)
      << "two node visits, two parent candidates, and one segment are counted";
}

TEST(StrokeHitTest,
     SegmentBudgetExhaustionDiscardsAHitSeenEarlierInSameStroke) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "polyline", LayerClass::Base, {{0, 0}, {10, 0}, {20, 100}}, 2,
      {0, -1, 20, 102})));

  const auto result = findTopmostStrokeHit(
      document, sweep({5, -5}, {5, 5}, 1, 3));

  EXPECT_EQ(result.status, StrokeHitTestStatus::BudgetExhausted);
  EXPECT_FALSE(result.token.has_value())
      << "a partial candidate can never escape a depleted budget";
  EXPECT_EQ(result.workConsumed, 3U);
}

TEST(StrokeHitTest, AmpleBudgetReportsNoHitWithoutFabricatingAToken) {
  Document document;
  ASSERT_TRUE(document.add(worldStroke(
      "far", LayerClass::Base, {{100, 100}, {120, 100}}, 2,
      {100, 99, 20, 2})));

  const auto result = findTopmostStrokeHit(
      document, sweep({0, 0}, {10, 0}, 1));

  EXPECT_EQ(result.status, StrokeHitTestStatus::NoHit);
  EXPECT_FALSE(result.token.has_value());
  EXPECT_GT(result.workConsumed, 0U);
}

}  // namespace
}  // namespace canvas::document
