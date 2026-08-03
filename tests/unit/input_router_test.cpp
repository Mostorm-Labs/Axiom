#include "canvas/input/input_router.h"

#include <gtest/gtest.h>

#include <optional>

using namespace canvas;

namespace {

const std::optional<document::NodeId> kWebId{"web-1"};

}  // namespace

TEST(InputRouterTest, DrawPenOnEmbeddedRoutesToAnnotationWithParent) {
  input::InputRouter router;

  const auto result = router.route(input::PointerKind::Pen, kWebId);

  EXPECT_EQ(result.target, input::InputTarget::Annotation);
  ASSERT_TRUE(result.parentId.has_value());
  EXPECT_EQ(*result.parentId, "web-1");
}

TEST(InputRouterTest, SelectTouchAndInteractTouchRouteToExpectedTargets) {
  input::InputRouter router;
  router.setMode(input::InputMode::Select);
  const auto selected = router.route(input::PointerKind::Touch, kWebId);
  EXPECT_EQ(selected.target, input::InputTarget::Selection);
  ASSERT_TRUE(selected.parentId.has_value());
  EXPECT_EQ(*selected.parentId, "web-1");

  router.setMode(input::InputMode::Interact);
  router.setActiveEmbeddedNode(kWebId);
  const auto interacted = router.route(input::PointerKind::Touch, kWebId);
  EXPECT_EQ(interacted.target, input::InputTarget::EmbeddedSurface);
  ASSERT_TRUE(interacted.parentId.has_value());
  EXPECT_EQ(*interacted.parentId, "web-1");
}

TEST(InputRouterTest, PenRemainsAnnotationFirstDuringEmbeddedInteraction) {
  input::InputRouter router;
  router.setMode(input::InputMode::Interact);
  router.setActiveEmbeddedNode(kWebId);

  const auto result = router.route(input::PointerKind::Pen, kWebId);

  EXPECT_EQ(result.target, input::InputTarget::Annotation);
  ASSERT_TRUE(result.parentId.has_value());
  EXPECT_EQ(*result.parentId, "web-1");
}

TEST(InputRouterTest, TouchDrawDefaultsToViewportAndFingerDrawUsesBaseCanvas) {
  input::InputRouter router;

  const auto defaultResult = router.route(input::PointerKind::Touch, std::nullopt);
  EXPECT_EQ(defaultResult.target, input::InputTarget::Viewport);
  EXPECT_FALSE(defaultResult.parentId.has_value());

  router.setFingerDrawEnabled(true);
  const auto fingerDrawResult =
      router.route(input::PointerKind::Touch, std::nullopt);
  EXPECT_EQ(fingerDrawResult.target, input::InputTarget::BaseCanvas);
  EXPECT_FALSE(fingerDrawResult.parentId.has_value());
}

TEST(InputRouterTest, MouseAndInactiveEmbeddedInteractionStayViewport) {
  input::InputRouter router;
  router.setMode(input::InputMode::Interact);

  const auto mouseResult = router.route(input::PointerKind::Mouse, kWebId);
  EXPECT_EQ(mouseResult.target, input::InputTarget::Viewport);
  EXPECT_FALSE(mouseResult.parentId.has_value());

  router.setActiveEmbeddedNode(kWebId);
  const auto otherResult =
      router.route(input::PointerKind::Touch,
                   std::optional<document::NodeId>{"web-2"});
  EXPECT_EQ(otherResult.target, input::InputTarget::Viewport);
  EXPECT_FALSE(otherResult.parentId.has_value());
}

TEST(InputRouterTest, SelectPreservesParentOnlyWhenEmbeddedHit) {
  input::InputRouter router;
  router.setMode(input::InputMode::Select);

  const auto noHit = router.route(input::PointerKind::Mouse, std::nullopt);
  EXPECT_EQ(noHit.target, input::InputTarget::Selection);
  EXPECT_FALSE(noHit.parentId.has_value());

  const auto hit = router.route(input::PointerKind::Mouse, kWebId);
  EXPECT_EQ(hit.target, input::InputTarget::Selection);
  ASSERT_TRUE(hit.parentId.has_value());
  EXPECT_EQ(*hit.parentId, "web-1");
}

TEST(InputRouterTest, FingerDrawOnlyChangesTouchInDrawMode) {
  input::InputRouter router;
  router.setFingerDrawEnabled(true);
  router.setMode(input::InputMode::Select);

  const auto result = router.route(input::PointerKind::Touch, kWebId);

  EXPECT_EQ(result.target, input::InputTarget::Selection);
  ASSERT_TRUE(result.parentId.has_value());
  EXPECT_EQ(*result.parentId, "web-1");
}
