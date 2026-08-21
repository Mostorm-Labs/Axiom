#include "canvas/poc03/large_scene.h"

#include <cstddef>
#include <vector>

#include "gtest/gtest.h"

namespace canvas::poc05 {
namespace {

constexpr std::size_t passIndex(poc03::LogicalPass pass) {
    return static_cast<std::size_t>(pass);
}

TEST(Poc03FrameContract, ExternalSurfacePassRemainsReservedAndEmpty) {
    poc03::Document document;
    const poc03::RuntimeScene scene =
        poc03::SceneCompiler().CompileFull(document);
    const poc03::ViewState view{
        7U, 1U, 1U, poc03::Bounds{0.0F, 0.0F, 800.0F, 600.0F},
        1.0F, 1.0F, 800U, 600U};
    const poc03::ViewQueryResult query =
        poc03::QueryView(scene, view, std::nullopt);
    const poc03::FrameGraph graph = poc03::BuildFrame(scene, query, {});
    const auto& external = graph.logical_passes[
        passIndex(poc03::LogicalPass::kExternalSurface)];
    const auto& overlay = graph.logical_passes[
        passIndex(poc03::LogicalPass::kOverlay)];

    EXPECT_TRUE(external.reserved);
    EXPECT_TRUE(external.item_ids.empty());
    EXPECT_EQ(external.dependencies,
              std::vector<poc03::LogicalPass>{poc03::LogicalPass::kInk});
    EXPECT_EQ(overlay.dependencies,
              std::vector<poc03::LogicalPass>{
                  poc03::LogicalPass::kExternalSurface});
    EXPECT_TRUE(poc03::ComposeSceneDrawList(graph).empty());
}

}  // namespace
}  // namespace canvas::poc05
