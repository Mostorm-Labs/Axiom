#include "canvas/poc05/hybrid_surface.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"

namespace canvas::poc05 {
namespace {

class FakeProjector final : public RuntimeViewProjector {
public:
    float scale = 1.0F;
    float originX = 0.0F;
    float originY = 0.0F;
    bool worldToViewLogical(CanvasViewHandle view, CanvasPointF worldPoint,
                            CanvasPointF* viewLogicalPoint,
                            std::string* error) const override {
        if (view == CANVAS_INVALID_HANDLE || viewLogicalPoint == nullptr) {
            *error = "invalid fake projection";
            return false;
        }
        *viewLogicalPoint = CanvasPointF{
            (worldPoint.x - originX) * scale,
            (worldPoint.y - originY) * scale};
        return true;
    }
};

class FakeBackend final : public PlatformOverlayBackend {
public:
    struct Surface {
        SurfaceKind kind;
        bool alive = false;
        PlacementCommand command;
    };
    bool create(ExternalSurfaceId id, SurfaceKind kind,
                std::string* error) override {
        const auto found = surfaces.find(id);
        if (found != surfaces.end()) {
            if (found->second.alive) {
                *error = "duplicate fake surface";
                return false;
            }
            found->second = Surface{kind, true, {}};
            return true;
        }
        surfaces.emplace(id, Surface{kind, true, {}});
        return true;
    }
    bool apply(const PlacementCommand& command, std::string*) override {
        auto found = surfaces.find(command.id);
        if (found == surfaces.end() || !found->second.alive) {
            return false;
        }
        found->second.command = command;
        commands.push_back(command);
        return true;
    }
    void destroy(ExternalSurfaceId id) override {
        auto found = surfaces.find(id);
        if (found != surfaces.end()) {
            found->second.alive = false;
        }
        ++destroyCount;
    }
    bool focus(ExternalSurfaceId id, std::string*) override {
        focused = id;
        return surfaces.contains(id) && surfaces.at(id).alive;
    }
    void focusCanvas() override {
        focused = 0U;
        ++canvasFocusCount;
    }
    std::unordered_map<ExternalSurfaceId, Surface> surfaces;
    std::vector<PlacementCommand> commands;
    ExternalSurfaceId focused = 0;
    std::size_t destroyCount = 0;
    std::size_t canvasFocusCount = 0;
};

ExternalSurfacePlaceholder makePlaceholder(ExternalSurfaceId id,
                                           SurfaceKind kind,
                                           std::uint32_t order = 0U) {
    return ExternalSurfacePlaceholder{
        1U, id, kind, CanvasRectF{100.0F, 50.0F, 200.0F, 200.0F},
        std::nullopt, 0.9F, order, 1U};
}

RuntimeViewFrame makeFrame(std::uint32_t generation = 1U,
                           std::uint64_t frameRevision = 1U,
                           std::uint64_t viewportRevision = 1U) {
    CanvasCameraStateV1 camera{};
    camera.struct_size = sizeof(CanvasCameraStateV1);
    camera.abi_version = CANVAS_RUNTIME_ABI_VERSION;
    camera.scale = 1.0F;
    camera.viewport_revision = viewportRevision;
    CanvasSurfaceStateV1 surface{};
    surface.struct_size = sizeof(CanvasSurfaceStateV1);
    surface.abi_version = CANVAS_RUNTIME_ABI_VERSION;
    surface.width_pixels = 1600U;
    surface.height_pixels = 1200U;
    surface.device_pixel_ratio = 2.0F;
    surface.target_generation = generation;
    surface.color_space = kCanvasColorSpaceSrgb;
    return RuntimeViewFrame{7U, camera, surface, frameRevision};
}

CanvasStatus successfulProjection(CanvasViewHandle view, CanvasPointF world,
                                  CanvasPointF* logical) {
    if (view == CANVAS_INVALID_HANDLE || logical == nullptr) {
        return kCanvasStatusInvalidArgument;
    }
    *logical = CanvasPointF{world.x * 2.0F, world.y * 2.0F};
    return kCanvasStatusOk;
}

TEST(CAbiProjector, DelegatesToStableWorldToScreenFunction) {
    CAbiRuntimeViewProjector projector(successfulProjection);
    CanvasPointF result{};
    std::string error;
    EXPECT_TRUE(projector.worldToViewLogical(
        9U, CanvasPointF{3.0F, 4.0F}, &result, &error));
    EXPECT_FLOAT_EQ(result.x, 6.0F);
    EXPECT_FLOAT_EQ(result.y, 8.0F);
}

TEST(Registry, RegisterUpdateAndRejectInvalidOrDuplicateIds) {
    FakeProjector projector;
    FakeBackend backend;
    ExternalSurfaceRegistry registry(projector, backend);
    std::string error;
    auto placeholder = makePlaceholder(11U, SurfaceKind::kWebView);
    EXPECT_TRUE(registry.registerSurface(placeholder, &error)) << error;
    EXPECT_FALSE(registry.registerSurface(placeholder, &error));
    placeholder.worldBounds.width = 0.0F;
    EXPECT_FALSE(registry.updateSurface(placeholder, &error));
}

TEST(Registry, ProjectsThroughStableViewContractAndAppliesDprClip) {
    FakeProjector projector;
    FakeBackend backend;
    ExternalSurfaceRegistry registry(projector, backend);
    std::string error;
    auto web = makePlaceholder(11U, SurfaceKind::kWebView, 2U);
    auto video = makePlaceholder(12U, SurfaceKind::kVideo, 1U);
    video.worldBounds = CanvasRectF{700.0F, 500.0F, 200.0F, 200.0F};
    video.worldClip = CanvasRectF{750.0F, 550.0F, 100.0F, 100.0F};
    ASSERT_TRUE(registry.registerSurface(web, &error));
    ASSERT_TRUE(registry.registerSurface(video, &error));
    ASSERT_TRUE(registry.markReady(11U, &error));
    ASSERT_TRUE(registry.markFailed(12U, &error));
    ASSERT_TRUE(registry.applyFrame(makeFrame(1U, 4U, 3U), &error)) << error;
    ASSERT_EQ(backend.commands.size(), 2U);
    EXPECT_EQ(backend.commands[0].id, 12U);
    EXPECT_EQ(backend.commands[0].layer,
              OverlayLayer::kAboveCanvasBelowProductUi);
    EXPECT_TRUE(backend.commands[0].failurePlaceholder);
    EXPECT_EQ(backend.commands[0].viewportRevision, 3U);
    EXPECT_EQ(backend.commands[1].id, 11U);
    EXPECT_TRUE(backend.commands[1].contentVisible);
    EXPECT_FLOAT_EQ(backend.commands[1].deviceBounds.x, 200.0F);
    EXPECT_FLOAT_EQ(backend.commands[1].deviceBounds.height, 400.0F);
    EXPECT_FLOAT_EQ(backend.commands[0].relativeDeviceClip.x, 100.0F);
    EXPECT_FLOAT_EQ(backend.commands[0].relativeDeviceClip.height, 100.0F);
}

TEST(Registry, RuntimeOwnsCameraMathInsteadOfRegistry) {
    FakeProjector projector;
    projector.scale = 2.0F;
    projector.originX = 40.0F;
    projector.originY = 20.0F;
    FakeBackend backend;
    ExternalSurfaceRegistry registry(projector, backend);
    std::string error;
    ASSERT_TRUE(registry.registerSurface(
        makePlaceholder(1U, SurfaceKind::kWebView), &error));
    ASSERT_TRUE(registry.applyFrame(makeFrame(), &error));
    const PlacementCommand& command = backend.commands.back();
    EXPECT_FLOAT_EQ(command.deviceBounds.x, 240.0F);
    EXPECT_FLOAT_EQ(command.deviceBounds.y, 120.0F);
    EXPECT_FLOAT_EQ(command.deviceBounds.width, 800.0F);
}

TEST(Registry, LifecycleFocusPageBackgroundAndStaleFramesAreBounded) {
    FakeProjector projector;
    FakeBackend backend;
    ExternalSurfaceRegistry registry(projector, backend);
    std::string error;
    ASSERT_TRUE(registry.registerSurface(
        makePlaceholder(1U, SurfaceKind::kWebView), &error));
    ASSERT_TRUE(registry.markReady(1U, &error));
    ASSERT_TRUE(registry.applyFrame(makeFrame(1U, 8U, 8U), &error));
    ASSERT_TRUE(registry.focusExternal(1U, &error));
    EXPECT_EQ(registry.focusedSurface(), std::optional<ExternalSurfaceId>(1U));
    EXPECT_TRUE(registry.applyFrame(makeFrame(1U, 7U, 7U), &error));
    EXPECT_EQ(registry.diagnostics().staleFrameCount, 1U);
    registry.setBackgrounded(true);
    ASSERT_TRUE(registry.applyFrame(makeFrame(1U, 9U, 9U), &error));
    EXPECT_FALSE(backend.commands.back().visible);
    registry.setBackgrounded(false);
    registry.setActivePage(2U);
    ASSERT_TRUE(registry.applyFrame(makeFrame(1U, 10U, 10U), &error));
    EXPECT_FALSE(backend.commands.back().visible);
    ASSERT_TRUE(registry.unregisterSurface(1U, &error));
    EXPECT_EQ(registry.diagnostics().activeSurfaceCount, 0U);
    EXPECT_EQ(registry.diagnostics().materializedSurfaceCount, 0U);
}

TEST(Registry, GenerationResetDestroysOldPlatformResources) {
    FakeProjector projector;
    FakeBackend backend;
    ExternalSurfaceRegistry registry(projector, backend);
    std::string error;
    ASSERT_TRUE(registry.registerSurface(
        makePlaceholder(3U, SurfaceKind::kVideo), &error));
    ASSERT_TRUE(registry.applyFrame(makeFrame(1U), &error));
    const std::size_t destroyedBefore = backend.destroyCount;
    ASSERT_TRUE(registry.applyFrame(makeFrame(2U), &error));
    EXPECT_GT(backend.destroyCount, destroyedBefore);
    EXPECT_EQ(registry.diagnostics().materializedSurfaceCount, 1U);
}

TEST(Registry, RejectsMalformedStableAbiSnapshot) {
    FakeProjector projector;
    FakeBackend backend;
    ExternalSurfaceRegistry registry(projector, backend);
    std::string error;
    RuntimeViewFrame frame = makeFrame();
    frame.camera.abi_version = 99U;
    EXPECT_FALSE(registry.applyFrame(frame, &error));
    EXPECT_EQ(registry.diagnostics().invalidFrameCount, 1U);
}

}  // namespace
}  // namespace canvas::poc05
