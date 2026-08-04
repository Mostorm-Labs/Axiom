#include "platform/macos/macos_tablet_input.h"
#include "platform/macos/macos_tablet_pointer_bridge.h"
#include "platform/macos/macos_whiteboard_input.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace canvas::macos {
namespace {

constexpr std::uint64_t capability(MacTabletCapability value) {
  return static_cast<std::uint64_t>(value);
}

MacTabletDeviceProfile profile(
    std::uint64_t deviceId, MacTabletTool tool = MacTabletTool::Pen,
    bool pressureSupported = true) {
  MacTabletDeviceProfile result;
  result.identity.deviceId = deviceId;
  result.identity.pointingDeviceId = deviceId + 100;
  result.identity.systemTabletId = deviceId + 200;
  result.identity.uniqueId = deviceId + 300;
  result.capabilityMask = capability(MacTabletCapability::DeviceId) |
                          capability(MacTabletCapability::TiltX) |
                          capability(MacTabletCapability::TiltY);
  if (pressureSupported) {
    result.capabilityMask |= capability(MacTabletCapability::Pressure);
  }
  result.tool = tool;
  return result;
}

RawMacTabletProximityEvent proximity(const MacTabletDeviceProfile& device,
                                     bool entering) {
  RawMacTabletProximityEvent raw;
  raw.profile = device;
  raw.timestampSeconds = 0.5;
  raw.entering = entering;
  return raw;
}

RawMacTabletPointEvent point(
    std::uint64_t deviceId, MacTabletPointPhase phase,
    double timestampSeconds, float x, float y, double pressure,
    MacTabletPointSource source = MacTabletPointSource::AssociatedMouse) {
  RawMacTabletPointEvent raw;
  raw.localPosition = {x, y};
  raw.boundsSize = {400.0F, 300.0F};
  raw.viewFlipped = true;
  raw.backingScale = 2.0;
  raw.timestampSeconds = timestampSeconds;
  raw.pressure = pressure;
  raw.tiltScaled = {0.25F, -0.5F};
  raw.tangentialPressure = -0.2;
  raw.rotationDegrees = 30.0;
  raw.buttonMask = 1;
  raw.deviceId = deviceId;
  raw.source = source;
  raw.phase = phase;
  if (source == MacTabletPointSource::AssociatedMouse) {
    raw.eventNumber = static_cast<std::int64_t>(timestampSeconds * 10.0);
  }
  return raw;
}

struct ControllerResults {
  std::array<MacosWhiteboardInputResult,
             MacosTabletPointerOutput::capacity>
      values;
  std::size_t count = 0;

  const MacosWhiteboardInputResult& operator[](
      std::size_t index) const noexcept {
    return values[index];
  }
};

ControllerResults feed(const MacosTabletSessionOutput& tabletOutput,
                       MacosWhiteboardInput& controller) {
  ControllerResults results;
  const MacosTabletPointerOutput pointerOutput =
      MacosTabletPointerBridge::convertOutput(tabletOutput);
  for (std::size_t index = 0; index < pointerOutput.size(); ++index) {
    results.values[results.count++] = controller.consume(pointerOutput[index]);
  }
  return results;
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

TEST(MacosTabletWhiteboardInputTest,
     AssociatedAndNativePenSamplesCommitOneBaseStroke) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  MacosTabletSession session;
  ASSERT_TRUE(session.consumeProximity(proximity(profile(7), true)).empty());

  const ControllerResults down = feed(
      session.consumePoint(point(7, MacTabletPointPhase::Down, 1.0,
                                 20.0F, 30.0F, 0.3)),
      controller);
  const ControllerResults exactNative = feed(
      session.consumePoint(point(
          7, MacTabletPointPhase::NativeUpdate, 1.0, 20.0F, 30.0F, 0.3,
          MacTabletPointSource::NativeTabletPoint)),
      controller);
  const ControllerResults nativeMove = feed(
      session.consumePoint(point(
          7, MacTabletPointPhase::NativeUpdate, 2.0, 50.0F, 60.0F, 0.8,
          MacTabletPointSource::NativeTabletPoint)),
      controller);
  const ControllerResults exactAssociated = feed(
      session.consumePoint(point(7, MacTabletPointPhase::Move, 2.0,
                                 50.0F, 60.0F, 0.8)),
      controller);
  const ControllerResults up = feed(
      session.consumePoint(point(7, MacTabletPointPhase::Up, 3.0,
                                 80.0F, 90.0F, 0.9)),
      controller);

  ASSERT_EQ(down.count, 1U);
  EXPECT_EQ(down[0].kind, MacosWhiteboardInputResultKind::Began);
  EXPECT_EQ(down[0].layer, document::LayerClass::Base);
  EXPECT_EQ(exactNative.count, 0U);
  ASSERT_EQ(nativeMove.count, 1U);
  EXPECT_EQ(nativeMove[0].kind, MacosWhiteboardInputResultKind::Changed);
  EXPECT_EQ(exactAssociated.count, 0U);
  ASSERT_EQ(up.count, 1U);
  EXPECT_EQ(up[0].kind, MacosWhiteboardInputResultKind::Finished);
  ASSERT_EQ(document->nodes().size(), 1U);
  const auto& completed = stroke(document->nodes().front());
  ASSERT_EQ(completed.points.size(), 3U)
      << "native and associated copies must not create two strokes";
  EXPECT_EQ(completed.points[0].position, (core::Vec2{20.0F, 30.0F}));
  EXPECT_FLOAT_EQ(completed.points[0].pressure, 0.3F);
  EXPECT_EQ(completed.points[1].position, (core::Vec2{50.0F, 60.0F}));
  EXPECT_FLOAT_EQ(completed.points[1].pressure, 0.8F);
  EXPECT_EQ(completed.points[2].position, (core::Vec2{80.0F, 90.0F}));
  EXPECT_FLOAT_EQ(completed.points[2].pressure, 0.9F);
}

TEST(MacosTabletWhiteboardInputTest,
     PenOnEmbeddedNodeCommitsParentNormalizedAnnotation) {
  auto document = std::make_shared<document::Document>();
  ASSERT_TRUE(document->add(embedded("web", {80, 40, 160, 100})));
  MacosWhiteboardInput controller(document);
  MacosTabletSession session;
  ASSERT_TRUE(session.consumeProximity(proximity(profile(8), true)).empty());

  const ControllerResults down = feed(
      session.consumePoint(point(8, MacTabletPointPhase::Down, 1.0,
                                 100.0F, 60.0F, 0.5)),
      controller);
  const ControllerResults up = feed(
      session.consumePoint(point(8, MacTabletPointPhase::Up, 2.0,
                                 180.0F, 100.0F, 0.7)),
      controller);

  ASSERT_EQ(down.count, 1U);
  EXPECT_EQ(down[0].kind, MacosWhiteboardInputResultKind::Began);
  EXPECT_EQ(down[0].layer, document::LayerClass::Annotation);
  ASSERT_EQ(up.count, 1U);
  EXPECT_EQ(up[0].kind, MacosWhiteboardInputResultKind::Finished);
  ASSERT_EQ(document->nodes().size(), 2U);
  const document::Node& annotation = document->nodes().back();
  EXPECT_EQ(annotation.layer, document::LayerClass::Annotation);
  EXPECT_EQ(annotation.parentId,
            std::optional<document::NodeId>{"web"});
  ASSERT_EQ(stroke(annotation).points.size(), 2U);
  EXPECT_EQ(stroke(annotation).coordinateSpace,
            document::StrokeCoordinateSpace::ParentNormalized);
  EXPECT_EQ(stroke(annotation).points[0].position,
            (core::Vec2{0.125F, 0.2F}));
  EXPECT_EQ(stroke(annotation).points[1].position,
            (core::Vec2{0.625F, 0.6F}));
}

TEST(MacosTabletWhiteboardInputTest,
     UnsupportedTabletToolsNeverMutateDocument) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  MacosTabletSession session;
  const std::array<MacTabletTool, 3> tools{
      MacTabletTool::Eraser, MacTabletTool::Cursor,
      MacTabletTool::Unknown};

  for (std::size_t index = 0; index < tools.size(); ++index) {
    const std::uint64_t deviceId = index + 1;
    ASSERT_TRUE(session.consumeProximity(
                           proximity(profile(deviceId, tools[index]), true))
                    .empty());
    EXPECT_EQ(feed(session.consumePoint(point(
                       deviceId, MacTabletPointPhase::Down, 1.0,
                       20.0F, 30.0F, 0.5)),
                   controller)
                  .count,
              0U);
    EXPECT_EQ(feed(session.consumePoint(point(
                       deviceId, MacTabletPointPhase::Up, 2.0,
                       40.0F, 50.0F, 0.5)),
                   controller)
                  .count,
              0U);
  }

  EXPECT_FALSE(controller.active());
  EXPECT_TRUE(document->nodes().empty());
}

TEST(MacosTabletWhiteboardInputTest,
     InteractModePenOutputFailsClosedBeforeDocumentMutation) {
  auto document = std::make_shared<document::Document>();
  ASSERT_TRUE(document->add(embedded("web", {0, 0, 200, 200})));
  MacosWhiteboardInput controller(document);
  ASSERT_EQ(controller.setMode(input::InputMode::Interact).kind,
            MacosWhiteboardInputResultKind::Ignored);
  MacosTabletSession session;
  ASSERT_TRUE(session.consumeProximity(proximity(profile(7), true)).empty());

  const ControllerResults down = feed(
      session.consumePoint(point(7, MacTabletPointPhase::Down, 1.0,
                                 20.0F, 30.0F, 0.5)),
      controller);
  const ControllerResults up = feed(
      session.consumePoint(point(7, MacTabletPointPhase::Up, 2.0,
                                 40.0F, 50.0F, 0.5)),
      controller);

  ASSERT_EQ(down.count, 1U);
  EXPECT_EQ(down[0].kind, MacosWhiteboardInputResultKind::Ignored);
  ASSERT_EQ(up.count, 1U);
  EXPECT_EQ(up[0].kind, MacosWhiteboardInputResultKind::Ignored);
  EXPECT_FALSE(controller.active());
  ASSERT_EQ(document->nodes().size(), 1U);
  EXPECT_EQ(document->nodes().front().id, "web");
}

TEST(MacosTabletWhiteboardInputTest,
     ProximityLeaveAndResetRollbackPenPreviewExactlyOnce) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  MacosTabletSession session;
  const MacTabletDeviceProfile device = profile(7);
  ASSERT_TRUE(session.consumeProximity(proximity(device, true)).empty());
  const ControllerResults firstDown = feed(
      session.consumePoint(point(7, MacTabletPointPhase::Down, 1.0,
                                 20.0F, 30.0F, 0.5)),
      controller);
  ASSERT_EQ(firstDown.count, 1U);
  ASSERT_EQ(firstDown[0].kind,
            MacosWhiteboardInputResultKind::Began);
  ASSERT_EQ(document->nodes().size(), 1U);

  const ControllerResults leave =
      feed(session.consumeProximity(proximity(device, false)), controller);
  EXPECT_EQ(leave.count, 1U);
  EXPECT_EQ(leave[0].kind, MacosWhiteboardInputResultKind::Cancelled);
  EXPECT_TRUE(document->nodes().empty());
  EXPECT_EQ(feed(session.consumeProximity(proximity(device, false)), controller)
                .count,
            0U);

  ASSERT_TRUE(session.consumeProximity(proximity(device, true)).empty());
  const ControllerResults secondDown = feed(
      session.consumePoint(point(7, MacTabletPointPhase::Down, 3.0,
                                 40.0F, 50.0F, 0.5)),
      controller);
  ASSERT_EQ(secondDown.count, 1U);
  ASSERT_EQ(secondDown[0].kind,
            MacosWhiteboardInputResultKind::Began);
  const ControllerResults reset = feed(session.reset(), controller);
  EXPECT_EQ(reset.count, 1U);
  EXPECT_EQ(reset[0].kind, MacosWhiteboardInputResultKind::Cancelled);
  EXPECT_TRUE(document->nodes().empty());
  EXPECT_EQ(feed(session.reset(), controller).count, 0U);
}

TEST(MacosTabletWhiteboardInputTest,
     AdditionalPenContactCannotCrossCommitSingleActiveController) {
  auto document = std::make_shared<document::Document>();
  MacosWhiteboardInput controller(document);
  MacosTabletSession session;
  ASSERT_TRUE(session.consumeProximity(proximity(profile(1), true)).empty());
  ASSERT_TRUE(session.consumeProximity(proximity(profile(2), true)).empty());

  const ControllerResults firstDown = feed(
      session.consumePoint(point(1, MacTabletPointPhase::Down, 1.0,
                                 20.0F, 30.0F, 0.5)),
      controller);
  const ControllerResults secondDown = feed(
      session.consumePoint(point(2, MacTabletPointPhase::Down, 1.1,
                                 100.0F, 110.0F, 0.6)),
      controller);
  const ControllerResults secondMove = feed(
      session.consumePoint(point(2, MacTabletPointPhase::Move, 1.2,
                                 120.0F, 130.0F, 0.7)),
      controller);
  const ControllerResults firstUp = feed(
      session.consumePoint(point(1, MacTabletPointPhase::Up, 2.0,
                                 40.0F, 50.0F, 0.8)),
      controller);
  const ControllerResults secondUp = feed(
      session.consumePoint(point(2, MacTabletPointPhase::Up, 2.1,
                                 140.0F, 150.0F, 0.9)),
      controller);

  ASSERT_EQ(firstDown.count, 1U);
  EXPECT_EQ(firstDown[0].kind, MacosWhiteboardInputResultKind::Began);
  ASSERT_EQ(secondDown.count, 1U);
  EXPECT_EQ(secondDown[0].kind, MacosWhiteboardInputResultKind::Ignored);
  ASSERT_EQ(secondMove.count, 1U);
  EXPECT_EQ(secondMove[0].kind, MacosWhiteboardInputResultKind::Ignored);
  ASSERT_EQ(firstUp.count, 1U);
  EXPECT_EQ(firstUp[0].kind, MacosWhiteboardInputResultKind::Finished);
  ASSERT_EQ(secondUp.count, 1U);
  EXPECT_EQ(secondUp[0].kind, MacosWhiteboardInputResultKind::Ignored);
  ASSERT_EQ(document->nodes().size(), 1U);
  ASSERT_EQ(stroke(document->nodes().front()).points.size(), 2U);
  EXPECT_EQ(stroke(document->nodes().front()).points[0].position,
            (core::Vec2{20.0F, 30.0F}));
  EXPECT_EQ(stroke(document->nodes().front()).points[1].position,
            (core::Vec2{40.0F, 50.0F}));
}

}  // namespace
}  // namespace canvas::macos
