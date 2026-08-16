#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "document.h"
#include "foundation.h"
#include "operations.h"
#include "scene_compiler.h"
#include "skia_scene_renderer.h"

namespace canvas::poc01::test {
namespace {

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

std::string ReadText(const std::filesystem::path& path) {
  const auto bytes = ReadBytes(path);
  return std::string(bytes.begin(), bytes.end());
}

TEST(SkiaRendererTest, DecodesPngLoadsRobotoAndRendersRgba) {
  auto assets = std::make_shared<AssetRegistry>();
  const auto checker = ReadBytes(
      std::filesystem::path(CANVAS_POC01_FIXTURE_DIR) / "checker.png");
  const auto font = ReadBytes(CANVAS_POC01_FONT_PATH);
  ASSERT_FALSE(checker.empty());
  ASSERT_FALSE(font.empty());
  ASSERT_EQ(assets->Register("checker.png", checker), CANVAS_POC_STATUS_OK);
  ASSERT_EQ(assets->Register("roboto.ttf", font), CANVAS_POC_STATUS_OK);
  Document document(assets, 800, 600, {244, 245, 247, 255});
  ASSERT_EQ(ApplyOperations(
                document,
                ReadText(std::filesystem::path(CANVAS_POC01_FIXTURE_DIR) /
                         "scene.ndjson")),
            CANVAS_POC_STATUS_OK);
  SkiaSceneRenderer renderer;
  std::vector<uint8_t> rgba;
  const RuntimeScene scene = SceneCompiler().Compile(document);
  ASSERT_EQ(renderer.RenderRaster(scene, document.assets(), &rgba),
            CANVAS_POC_STATUS_OK)
      << GetLastError();
  EXPECT_EQ(rgba.size(), 800U * 600U * 4U);
  EXPECT_NE(HashHex(HashBytes(rgba)), "00000000000000000000000000000000");
  std::vector<uint8_t> cached_rgba;
  ASSERT_EQ(renderer.RenderRaster(scene, document.assets(), &cached_rgba),
            CANVAS_POC_STATUS_OK)
      << GetLastError();
  EXPECT_EQ(cached_rgba, rgba);
}

TEST(SkiaRendererTest, InvalidImageBytesReportAssetError) {
  auto assets = std::make_shared<AssetRegistry>();
  const std::vector<uint8_t> invalid = {1, 2, 3, 4};
  ASSERT_EQ(assets->Register("broken.png", invalid), CANVAS_POC_STATUS_OK);
  Document document(assets, 32, 32, {0, 0, 0, 255});
  const std::string replay =
      "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":1,\"type\":\"image\",\"order\":1,\"x\":0,\"y\":0,\"width\":32,\"height\":32,\"asset_key\":\"broken.png\"}}\n";
  ASSERT_EQ(ApplyOperations(document, replay), CANVAS_POC_STATUS_OK);
  std::vector<uint8_t> rgba;
  EXPECT_EQ(SkiaSceneRenderer().RenderRaster(
                SceneCompiler().Compile(document), document.assets(), &rgba),
            CANVAS_POC_STATUS_ASSET_ERROR);
}

}  // namespace
}  // namespace canvas::poc01::test
