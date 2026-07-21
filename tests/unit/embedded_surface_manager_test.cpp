#include "canvas/embed/embedded_surface.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using canvas::core::Rect;
using canvas::document::Document;
using canvas::document::EmbeddedKind;
using canvas::document::EmbeddedNode;
using canvas::document::LayerClass;
using canvas::document::Node;
using canvas::embed::EmbeddedSurface;
using canvas::embed::EmbeddedSurfaceFactory;
using canvas::embed::EmbeddedSurfaceManager;

struct SurfaceState {
  Rect bounds;
  bool interactive = false;
  bool visible = false;
  int boundsUpdates = 0;
  int interactiveUpdates = 0;
  int visibleUpdates = 0;
  bool destroyed = false;
};

class FakeSurface final : public EmbeddedSurface {
 public:
  explicit FakeSurface(std::shared_ptr<SurfaceState> state)
      : state_(std::move(state)) {}
  ~FakeSurface() override { state_->destroyed = true; }

  void setBounds(Rect bounds) override {
    state_->bounds = bounds;
    ++state_->boundsUpdates;
  }

  void setInteractive(bool interactive) override {
    state_->interactive = interactive;
    ++state_->interactiveUpdates;
  }

  void setVisible(bool visible) override {
    state_->visible = visible;
    ++state_->visibleUpdates;
  }

 private:
  std::shared_ptr<SurfaceState> state_;
};

class FakeSurfaceFactory final : public EmbeddedSurfaceFactory {
 public:
  std::unique_ptr<EmbeddedSurface> create(const Node& node) override {
    ++createCalls;
    createdNodeIds.push_back(node.id);
    if (throwOnCreate) {
      throw std::runtime_error("surface creation failed");
    }
    if (returnNull) {
      return nullptr;
    }
    auto state = std::make_shared<SurfaceState>();
    states.push_back(state);
    return std::make_unique<FakeSurface>(std::move(state));
  }

  int createCalls = 0;
  bool returnNull = false;
  bool throwOnCreate = false;
  std::vector<canvas::document::NodeId> createdNodeIds;
  std::vector<std::shared_ptr<SurfaceState>> states;
};

Node embeddedNode(const char* id, EmbeddedKind kind, Rect bounds) {
  return Node{id, LayerClass::Embedded, bounds, {},
              EmbeddedNode{kind, "source", "title"}};
}

TEST(EmbeddedSurfaceManagerTest, KeepsActiveWebAndVisibleVideoLive) {
  Document document;
  ASSERT_TRUE(document.add(
      embeddedNode("web", EmbeddedKind::Web, {0, 0, 300, 200})));
  ASSERT_TRUE(document.add(
      embeddedNode("video", EmbeddedKind::Video, {320, 0, 300, 200})));
  FakeSurfaceFactory factory;
  EmbeddedSurfaceManager manager(factory);

  manager.sync(document, {0, 0, 800, 600},
               canvas::document::NodeId{"web"});

  ASSERT_EQ(factory.createCalls, 2);
  ASSERT_EQ(manager.liveCount(), 2U);
  ASSERT_EQ(factory.states.size(), 2U);
  EXPECT_TRUE(factory.states[0]->interactive);
  EXPECT_FALSE(factory.states[1]->interactive);
  EXPECT_TRUE(factory.states[0]->visible);
  EXPECT_TRUE(factory.states[1]->visible);

  manager.sync(document, {310, 0, 400, 300}, std::nullopt);

  EXPECT_EQ(manager.liveCount(), 1U);
  EXPECT_TRUE(factory.states[0]->destroyed);
  EXPECT_FALSE(factory.states[1]->destroyed);
}

TEST(EmbeddedSurfaceManagerTest, ReusesSurfaceAndUpdatesItsProperties) {
  Document document;
  ASSERT_TRUE(document.add(
      embeddedNode("video", EmbeddedKind::Video, {10, 20, 300, 200})));
  FakeSurfaceFactory factory;
  EmbeddedSurfaceManager manager(factory);
  manager.sync(document, {0, 0, 800, 600}, std::nullopt);
  ASSERT_EQ(factory.states.size(), 1U);
  const auto state = factory.states.front();
  EXPECT_EQ(state->bounds, (Rect{10, 20, 300, 200}));
  EXPECT_FALSE(state->interactive);
  EXPECT_TRUE(state->visible);

  ASSERT_TRUE(document.setBounds("video", {30, 40, 320, 180}));
  manager.sync(document, {0, 0, 800, 600},
               canvas::document::NodeId{"video"});

  EXPECT_EQ(factory.createCalls, 1);
  EXPECT_EQ(manager.liveCount(), 1U);
  EXPECT_EQ(state->bounds, (Rect{30, 40, 320, 180}));
  EXPECT_TRUE(state->interactive);
  EXPECT_TRUE(state->visible);
  EXPECT_EQ(state->boundsUpdates, 2);
  EXPECT_EQ(state->interactiveUpdates, 2);
  EXPECT_EQ(state->visibleUpdates, 2);
}

TEST(EmbeddedSurfaceManagerTest, SkipsInactiveWebAndRemovesOffscreenVideo) {
  Document document;
  ASSERT_TRUE(document.add(
      embeddedNode("web", EmbeddedKind::Web, {0, 0, 300, 200})));
  ASSERT_TRUE(document.add(
      embeddedNode("video", EmbeddedKind::Video, {320, 0, 300, 200})));
  FakeSurfaceFactory factory;
  EmbeddedSurfaceManager manager(factory);

  manager.sync(document, {0, 0, 800, 600}, std::nullopt);
  ASSERT_EQ(factory.createCalls, 1);
  ASSERT_EQ(factory.createdNodeIds.front(), "video");
  ASSERT_EQ(factory.states.size(), 1U);

  manager.sync(document, {0, 250, 800, 300}, std::nullopt);

  EXPECT_EQ(manager.liveCount(), 0U);
  EXPECT_TRUE(factory.states.front()->destroyed);
}

TEST(EmbeddedSurfaceManagerTest, EdgeTouchDoesNotIntersectViewport) {
  Document document;
  ASSERT_TRUE(document.add(
      embeddedNode("right", EmbeddedKind::Video, {100, 0, 20, 20})));
  ASSERT_TRUE(document.add(
      embeddedNode("bottom", EmbeddedKind::Video, {0, 100, 20, 20})));
  FakeSurfaceFactory factory;
  EmbeddedSurfaceManager manager(factory);

  manager.sync(document, {0, 0, 100, 100}, std::nullopt);

  EXPECT_EQ(factory.createCalls, 0);
  EXPECT_EQ(manager.liveCount(), 0U);
}

TEST(EmbeddedSurfaceManagerTest, SkipsWrongLayerAndWrongPayload) {
  Document document;
  Node wrongLayer =
      embeddedNode("base-video", EmbeddedKind::Video, {0, 0, 100, 100});
  wrongLayer.layer = LayerClass::Base;
  ASSERT_TRUE(document.add(std::move(wrongLayer)));
  Node wrongPayload;
  wrongPayload.id = "stroke-on-embedded-layer";
  wrongPayload.layer = LayerClass::Embedded;
  wrongPayload.bounds = {0, 0, 100, 100};
  ASSERT_TRUE(document.add(std::move(wrongPayload)));
  FakeSurfaceFactory factory;
  EmbeddedSurfaceManager manager(factory);

  manager.sync(document, {0, 0, 100, 100},
               canvas::document::NodeId{"stroke-on-embedded-layer"});

  EXPECT_EQ(factory.createCalls, 0);
  EXPECT_EQ(manager.liveCount(), 0U);
}

TEST(EmbeddedSurfaceManagerTest, NullFactoryResultIsSkippedAndCanBeRetried) {
  Document document;
  ASSERT_TRUE(document.add(
      embeddedNode("video", EmbeddedKind::Video, {0, 0, 100, 100})));
  FakeSurfaceFactory factory;
  factory.returnNull = true;
  EmbeddedSurfaceManager manager(factory);

  manager.sync(document, {0, 0, 100, 100}, std::nullopt);
  EXPECT_EQ(manager.liveCount(), 0U);

  factory.returnNull = false;
  manager.sync(document, {0, 0, 100, 100}, std::nullopt);

  EXPECT_EQ(factory.createCalls, 2);
  EXPECT_EQ(manager.liveCount(), 1U);
}

TEST(EmbeddedSurfaceManagerTest, FactoryExceptionDoesNotAddNullLiveEntry) {
  Document document;
  ASSERT_TRUE(document.add(
      embeddedNode("video", EmbeddedKind::Video, {0, 0, 100, 100})));
  FakeSurfaceFactory factory;
  factory.throwOnCreate = true;
  EmbeddedSurfaceManager manager(factory);

  EXPECT_THROW(manager.sync(document, {0, 0, 100, 100}, std::nullopt),
               std::runtime_error);
  EXPECT_EQ(manager.liveCount(), 0U);

  factory.throwOnCreate = false;
  EXPECT_NO_THROW(manager.sync(document, {0, 0, 100, 100}, std::nullopt));
  EXPECT_EQ(manager.liveCount(), 1U);
}

}  // namespace
