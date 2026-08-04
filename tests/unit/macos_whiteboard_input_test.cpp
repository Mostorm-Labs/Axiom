#include "platform/macos/macos_whiteboard_input.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace canvas::macos {
namespace {

constexpr float kStrokeWidth = 4.0F;

input::PointerSample pointer(input::PointerKind kind,
                             std::uint64_t pointerId,
                             input::PointerPhase phase, float x, float y) {
  input::PointerSample sample;
  sample.pointerId = pointerId;
  sample.timestampMicros = static_cast<std::uint64_t>(x + y + 100.0F);
  sample.screenPosition = {x, y};
  sample.kind = kind;
  sample.phase = phase;
  return sample;
}

input::PointerSample mouse(std::uint64_t pointerId,
                           input::PointerPhase phase, float x, float y) {
  return pointer(input::PointerKind::Mouse, pointerId, phase, x, y);
}

input::PointerSample pen(std::uint64_t pointerId,
                         input::PointerPhase phase, float x, float y) {
  return pointer(input::PointerKind::Pen, pointerId, phase, x, y);
}

document::Node embedded(std::string id, core::Rect bounds) {
  document::Node node;
  node.id = std::move(id);
  node.layer = document::LayerClass::Embedded;
  node.bounds = bounds;
  node.payload = document::EmbeddedNode{};
  return node;
}

const document::StrokeNode& stroke(const document::Node& node) {
  return std::get<document::StrokeNode>(node.payload);
}

TEST(MacosWhiteboardInputTest, BuildsBasePreviewAcrossDownMoveAndUp) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);

  const auto began = controller.consume(mouse(7, input::PointerPhase::Down,
                                               10.0F, 20.0F));
  ASSERT_EQ(began.kind, MacosWhiteboardInputResultKind::Began);
  EXPECT_EQ(began.layer, document::LayerClass::Base);
  ASSERT_TRUE(began.dirtyBounds.has_value());
  EXPECT_FALSE(began.fullRedraw);
  ASSERT_TRUE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_GE(stroke(document->nodes().front()).points.capacity(), 512U);

  const auto changed = controller.consume(mouse(
      7, input::PointerPhase::Move, 30.0F, 40.0F));
  ASSERT_EQ(changed.kind, MacosWhiteboardInputResultKind::Changed);
  EXPECT_EQ(changed.layer, document::LayerClass::Base);
  EXPECT_TRUE(changed.dirtyBounds.has_value());
  ASSERT_EQ(stroke(document->nodes().front()).points.size(), 2U);

  const auto finished = controller.consume(mouse(
      7, input::PointerPhase::Up, 50.0F, 60.0F));
  EXPECT_EQ(finished.kind, MacosWhiteboardInputResultKind::Finished);
  EXPECT_EQ(finished.layer, document::LayerClass::Base);
  EXPECT_TRUE(finished.dirtyBounds.has_value());
  EXPECT_FALSE(finished.fullRedraw);
  EXPECT_FALSE(controller.active());
  ASSERT_EQ(stroke(document->nodes().front()).points.size(), 3U);
  EXPECT_EQ(stroke(document->nodes().front()).coordinateSpace,
            document::StrokeCoordinateSpace::World);
  EXPECT_FALSE(document->nodes().front().parentId.has_value());
}

TEST(MacosWhiteboardInputTest,
     UsesTopmostEmbeddedHitAndCommitsParentNormalizedAnnotation) {
  auto document = std::make_shared<document::Document>();
  ASSERT_TRUE(document->add(embedded("lower", {0, 0, 200, 200})));
  ASSERT_TRUE(document->add(embedded("top", {100, 100, 200, 100})));
  MacosWhiteboardInput controller(document);

  const auto began = controller.consume(mouse(
      1, input::PointerPhase::Down, 150.0F, 150.0F));
  ASSERT_EQ(began.kind, MacosWhiteboardInputResultKind::Began);
  EXPECT_EQ(began.layer, document::LayerClass::Annotation);
  const auto finished = controller.consume(mouse(
      1, input::PointerPhase::Up, 200.0F, 175.0F));

  ASSERT_EQ(finished.kind, MacosWhiteboardInputResultKind::Finished);
  EXPECT_EQ(finished.layer, document::LayerClass::Annotation);
  ASSERT_EQ(document->nodes().size(), 3U);
  const auto& annotation = document->nodes().back();
  ASSERT_EQ(annotation.parentId, std::optional<document::NodeId>{"top"});
  EXPECT_EQ(annotation.layer, document::LayerClass::Annotation);
  ASSERT_EQ(stroke(annotation).points.size(), 2U);
  EXPECT_EQ(stroke(annotation).coordinateSpace,
            document::StrokeCoordinateSpace::ParentNormalized);
  EXPECT_EQ(stroke(annotation).points[0].position,
            (core::Vec2{0.25F, 0.5F}));
  EXPECT_EQ(stroke(annotation).points[1].position,
            (core::Vec2{0.5F, 0.75F}));
  EXPECT_FLOAT_EQ(stroke(annotation).width, kStrokeWidth / 100.0F);
}

TEST(MacosWhiteboardInputTest, CancelRollsBackPreviewExactlyOnce) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(3, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Began);

  const auto cancelled =
      controller.consume(mouse(3, input::PointerPhase::Cancel, 1, 2));
  EXPECT_EQ(cancelled.kind, MacosWhiteboardInputResultKind::Cancelled);
  EXPECT_EQ(cancelled.layer, document::LayerClass::Base);
  EXPECT_TRUE(cancelled.dirtyBounds.has_value());
  EXPECT_TRUE(document->nodes().empty());
  EXPECT_FALSE(controller.active());

  EXPECT_EQ(controller.consume(mouse(3, input::PointerPhase::Cancel, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_TRUE(document->nodes().empty());
}

TEST(MacosWhiteboardInputTest, ModeChangeCancelsBeforeChangingRouting) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(4, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Began);

  const auto invalidated = controller.setMode(input::InputMode::Select);
  EXPECT_EQ(invalidated.kind, MacosWhiteboardInputResultKind::Cancelled);
  EXPECT_TRUE(invalidated.dirtyBounds.has_value());
  EXPECT_FALSE(invalidated.fullRedraw);
  EXPECT_TRUE(document->nodes().empty());
  EXPECT_FALSE(controller.active());
  EXPECT_EQ(controller.consume(mouse(5, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Ignored);
}

TEST(MacosWhiteboardInputTest, DocumentReplacementCancelsOldPreviewFirst) {
  auto oldDocument = std::make_shared<document::Document>();
  auto nextDocument = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(oldDocument);
  ASSERT_EQ(controller.consume(mouse(4, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Began);

  const auto invalidated = controller.replaceDocument(nextDocument);
  EXPECT_EQ(invalidated.kind, MacosWhiteboardInputResultKind::Cancelled);
  EXPECT_TRUE(invalidated.dirtyBounds.has_value());
  EXPECT_TRUE(invalidated.fullRedraw);
  EXPECT_TRUE(oldDocument->nodes().empty());
  EXPECT_EQ(controller.document(), nextDocument);
  EXPECT_FALSE(controller.active());
  EXPECT_EQ(controller.consume(mouse(4, input::PointerPhase::Move, 3, 4)).kind,
            MacosWhiteboardInputResultKind::Ignored);
}

TEST(MacosWhiteboardInputTest, SelectAndInteractNeverCreateMouseStrokes) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);

  EXPECT_EQ(controller.setMode(input::InputMode::Select).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.setMode(input::InputMode::Interact).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(mouse(2, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_TRUE(document->nodes().empty());
}

TEST(MacosWhiteboardInputTest, RejectsTouchAndNonFiniteSamples) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  auto touch = mouse(1, input::PointerPhase::Down, 1, 2);
  touch.kind = input::PointerKind::Touch;
  auto nonFinitePosition = mouse(2, input::PointerPhase::Down, 1, 2);
  nonFinitePosition.screenPosition.x =
      std::numeric_limits<float>::quiet_NaN();
  auto nonFinitePressure = mouse(3, input::PointerPhase::Down, 1, 2);
  nonFinitePressure.pressure = std::numeric_limits<float>::infinity();
  auto zeroPointer = mouse(0, input::PointerPhase::Down, 1, 2);
  auto negativePressure = mouse(5, input::PointerPhase::Down, 1, 2);
  negativePressure.pressure = -0.01F;
  auto excessivePressure = mouse(6, input::PointerPhase::Down, 1, 2);
  excessivePressure.pressure = 1.01F;

  EXPECT_EQ(controller.consume(touch).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(nonFinitePosition).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(nonFinitePressure).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(zeroPointer).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(negativePressure).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(excessivePressure).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_TRUE(document->nodes().empty());

  ASSERT_EQ(controller.consume(mouse(4, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Began);
  auto nonFiniteMove = mouse(4, input::PointerPhase::Move, 3, 4);
  nonFiniteMove.tiltXDegrees =
      std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(controller.consume(nonFiniteMove).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_TRUE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(stroke(document->nodes().front()).points.size(), 1U);
}

TEST(MacosWhiteboardInputTest,
     AcceptsPenButNeverCrossesPointerKindForSameId) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);

  auto predictedPen = pen(9, input::PointerPhase::Down, 1, 2);
  predictedPen.predicted = true;
  EXPECT_EQ(controller.consume(predictedPen).kind,
            MacosWhiteboardInputResultKind::Ignored);
  ASSERT_EQ(controller.consume(pen(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  EXPECT_EQ(controller.consume(mouse(1, input::PointerPhase::Move, 30, 40)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(mouse(1, input::PointerPhase::Cancel, 30, 40)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_TRUE(controller.active());
  EXPECT_EQ(controller.consume(pen(1, input::PointerPhase::Move, 30, 40)).kind,
            MacosWhiteboardInputResultKind::Changed);
  EXPECT_EQ(controller.consume(pen(1, input::PointerPhase::Cancel, 30, 40)).kind,
            MacosWhiteboardInputResultKind::Cancelled);
  EXPECT_FALSE(controller.active());
  EXPECT_TRUE(document->nodes().empty());
}

TEST(MacosWhiteboardInputTest, PenBeginsOnlyInDrawMode) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);

  EXPECT_EQ(controller.setMode(input::InputMode::Select).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(pen(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.setMode(input::InputMode::Interact).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(pen(2, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_FALSE(controller.active());
  EXPECT_TRUE(document->nodes().empty());

  EXPECT_EQ(controller.setMode(input::InputMode::Draw).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(pen(3, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  EXPECT_EQ(controller.consume(pen(3, input::PointerPhase::Cancel, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Cancelled);
}

TEST(MacosWhiteboardInputTest, IgnoresOrphanAndMismatchedPhasesSafely) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  EXPECT_EQ(controller.consume(mouse(9, input::PointerPhase::Move, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(mouse(9, input::PointerPhase::Up, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Ignored);

  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Began);
  EXPECT_EQ(controller.consume(mouse(2, input::PointerPhase::Move, 3, 4)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(mouse(2, input::PointerPhase::Up, 3, 4)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_EQ(controller.consume(mouse(2, input::PointerPhase::Cancel, 3, 4)).kind,
            MacosWhiteboardInputResultKind::Ignored);
  EXPECT_TRUE(controller.active());
  EXPECT_EQ(document->nodes().size(), 1U);
}

TEST(MacosWhiteboardInputTest, AvoidsStrokeIdCollisions) {
  auto document = std::make_shared<document::Document>();
  document::Node collision;
  collision.id = "macos-stroke-1";
  ASSERT_TRUE(document->add(std::move(collision)));
  MacosWhiteboardInput controller(document);

  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 1, 2)).kind,
            MacosWhiteboardInputResultKind::Began);
  ASSERT_EQ(document->nodes().size(), 2U);
  EXPECT_EQ(document->nodes().back().id, "macos-stroke-2");
}

TEST(MacosWhiteboardInputTest, FinishesSinglePointStroke) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);

  EXPECT_EQ(controller.consume(mouse(1, input::PointerPhase::Up, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Finished);
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(stroke(document->nodes().front()).points.size(), 1U);
}

TEST(MacosWhiteboardInputTest, ParentFailureRemovesPreviewAndActiveState) {
  auto document = std::make_shared<document::Document>();
  ASSERT_TRUE(document->add(embedded("web", {0, 0, 100, 100})));
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  ASSERT_TRUE(document->mutate("web", [](document::Node& node) {
    node.bounds.width = 0.0F;
  }));

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Up, 30, 40));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_FALSE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(document->nodes().front().id, "web");
}

TEST(MacosWhiteboardInputTest, MissingPreviewFailsWithoutLeavingActiveState) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  ASSERT_TRUE(document->erase(document->nodes().front().id));

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Move, 30, 40));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_FALSE(controller.active());
  EXPECT_TRUE(document->nodes().empty());
}

TEST(MacosWhiteboardInputTest, ReplacedPreviewPayloadFailsInsteadOfClobbering) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  ASSERT_TRUE(document->mutate(previewId, [](document::Node& node) {
    node.payload = document::UnknownNode{"external", "{}"};
  }));

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Up, 10, 20));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_FALSE(controller.active());
  EXPECT_TRUE(document->nodes().empty());
}

TEST(MacosWhiteboardInputTest, ExternalStrokeMutationCannotBeSilentlyFinished) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  ASSERT_TRUE(document->mutate(previewId, [](document::Node& node) {
    std::get<document::StrokeNode>(node.payload).width = 42.0F;
  }));

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Up, 10, 20));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_TRUE(failed.fullRedraw);
  EXPECT_FALSE(controller.active());
  EXPECT_TRUE(document->nodes().empty());
}

TEST(MacosWhiteboardInputTest, ExternalAppendInvalidatesOwnedPreviewRevision) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  ASSERT_TRUE(document->appendStrokePoint(
      previewId, document::StrokePoint{{90, 90}, 0.5F, 99},
      core::Rect{85, 85, 10, 10}));

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Move, 30, 40));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_TRUE(failed.fullRedraw);
  EXPECT_FALSE(controller.active());
  EXPECT_TRUE(document->nodes().empty());
}

TEST(MacosWhiteboardInputTest, RecreatedSameIdNodeIsNeverErasedByStaleOwner) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  const auto oldIdentity = document->nodes().front().cacheIdentity;
  ASSERT_TRUE(document->erase(previewId));
  document::Node replacement;
  replacement.id = previewId;
  replacement.layer = document::LayerClass::Chrome;
  replacement.payload = document::UnknownNode{"replacement", "{}"};
  ASSERT_TRUE(document->add(std::move(replacement)));
  ASSERT_NE(document->nodes().front().cacheIdentity, oldIdentity);

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Move, 30, 40));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_TRUE(failed.fullRedraw);
  EXPECT_FALSE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(document->nodes().front().id, previewId);
  EXPECT_EQ(document->nodes().front().layer, document::LayerClass::Chrome);
  EXPECT_TRUE(std::holds_alternative<document::UnknownNode>(
      document->nodes().front().payload));
}

TEST(MacosWhiteboardInputTest, RecreatedSameIdNodeAlsoSurvivesRollback) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  ASSERT_TRUE(document->erase(previewId));
  document::Node replacement;
  replacement.id = previewId;
  replacement.layer = document::LayerClass::Chrome;
  replacement.payload = document::UnknownNode{"replacement", "{}"};
  ASSERT_TRUE(document->add(std::move(replacement)));
  const auto replacementIdentity = document->nodes().front().cacheIdentity;

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Cancel, 10, 20));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_TRUE(failed.fullRedraw);
  EXPECT_FALSE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(document->nodes().front().cacheIdentity, replacementIdentity);
  EXPECT_EQ(document->nodes().front().id, previewId);
}

TEST(MacosWhiteboardInputTest,
     NullDocumentReplacementPreservesDocumentAndActivePreview) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewIdentity = document->nodes().front().cacheIdentity;

  const auto failed = controller.replaceDocument(nullptr);
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_FALSE(failed.fullRedraw);
  EXPECT_EQ(controller.document(), document);
  EXPECT_TRUE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(document->nodes().front().cacheIdentity, previewIdentity);
}

TEST(MacosWhiteboardInputTest,
     InPlaceDocumentAssignmentMakesMoveFailWithoutErasingClone) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  const auto previewIdentity = document->nodes().front().cacheIdentity;
  const auto oldDocumentInstance = document->instanceId();
  document::Document clone = *document;
  *document = clone;
  ASSERT_NE(document->instanceId(), oldDocumentInstance);

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Move, 30, 40));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_TRUE(failed.fullRedraw);
  EXPECT_FALSE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(document->nodes().front().id, previewId);
  EXPECT_EQ(document->nodes().front().cacheIdentity, previewIdentity);
  EXPECT_EQ(stroke(document->nodes().front()).points.size(), 1U);
}

TEST(MacosWhiteboardInputTest,
     InPlaceDocumentAssignmentMakesUpFailWithoutErasingClone) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  document::Document clone = *document;
  *document = clone;

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Up, 30, 40));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_TRUE(failed.fullRedraw);
  EXPECT_FALSE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(document->nodes().front().id, previewId);
  EXPECT_EQ(stroke(document->nodes().front()).points.size(), 1U);
}

TEST(MacosWhiteboardInputTest,
     InPlaceDocumentAssignmentMakesCancelFailWithoutErasingClone) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  document::Document clone = *document;
  *document = clone;

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Cancel, 10, 20));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_TRUE(failed.fullRedraw);
  EXPECT_FALSE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(document->nodes().front().id, previewId);
}

TEST(MacosWhiteboardInputTest,
     ExternallyMovedOwnedPreviewFailsRollbackAndRequestsFullRedraw) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.consume(mouse(1, input::PointerPhase::Down, 10, 20)).kind,
            MacosWhiteboardInputResultKind::Began);
  const auto previewId = document->nodes().front().id;
  ASSERT_TRUE(document->setBounds(previewId, {1000, 1000, 20, 20}));

  const auto failed =
      controller.consume(mouse(1, input::PointerPhase::Cancel, 10, 20));
  EXPECT_EQ(failed.kind, MacosWhiteboardInputResultKind::Failed);
  EXPECT_TRUE(failed.fullRedraw);
  EXPECT_FALSE(controller.active());
  EXPECT_TRUE(document->nodes().empty());
}

}  // namespace
}  // namespace canvas::macos
