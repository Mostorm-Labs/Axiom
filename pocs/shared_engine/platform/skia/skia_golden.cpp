#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "document.h"
#include "foundation.h"
#include "operations.h"
#include "scene_compiler.h"
#include "skia_scene_renderer.h"

namespace {

namespace fs = std::filesystem;

std::vector<uint8_t> ReadBytes(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("failed to read " + path.string());
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

std::string ReadText(const fs::path& path) {
  const auto bytes = ReadBytes(path);
  return std::string(bytes.begin(), bytes.end());
}

}  // namespace

int main(int argc, char** argv) {
  try {
    fs::path output;
    bool update = false;
    for (int index = 1; index < argc; ++index) {
      const std::string argument(argv[index]);
      if (argument == "--update-golden") update = true;
      else if (argument.starts_with("--output=")) output = argument.substr(9);
      else throw std::runtime_error("unknown golden generator argument");
    }
    if (!update || output.empty()) {
      throw std::runtime_error(
          "golden generation requires explicit --update-golden --output=PATH");
    }
    auto assets = std::make_shared<canvas::poc01::AssetRegistry>();
    const auto checker =
        ReadBytes(fs::path(CANVAS_POC01_FIXTURE_DIR) / "checker.png");
    const auto font = ReadBytes(CANVAS_POC01_FONT_PATH);
    if (assets->Register("checker.png", checker) != CANVAS_POC_STATUS_OK ||
        assets->Register("roboto.ttf", font) != CANVAS_POC_STATUS_OK) {
      throw std::runtime_error("asset registration failed");
    }
    canvas::poc01::Document document(assets, 800, 600,
                                     {244, 245, 247, 255});
    const std::string replay =
        ReadText(fs::path(CANVAS_POC01_FIXTURE_DIR) / "scene.ndjson");
    if (canvas::poc01::ApplyOperations(document, replay) !=
        CANVAS_POC_STATUS_OK) {
      throw std::runtime_error(std::string(canvas::poc01::GetLastError()));
    }
    const canvas::poc01::RuntimeScene scene =
        canvas::poc01::SceneCompiler().Compile(document);
    std::vector<uint8_t> rgba;
    canvas::poc01::SkiaSceneRenderer renderer;
    if (renderer.RenderRaster(scene, document.assets(), &rgba) !=
        CANVAS_POC_STATUS_OK) {
      throw std::runtime_error(std::string(canvas::poc01::GetLastError()));
    }
    fs::create_directories(output.parent_path());
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(rgba.data()),
                 static_cast<std::streamsize>(rgba.size()));
    std::cout << "{\"backend\":\"skia-raster\",\"digest\":\""
              << document.Digest() << "\",\"pixel_hash\":\""
              << canvas::poc01::HashHex(canvas::poc01::HashBytes(rgba))
              << "\",\"bytes\":" << rgba.size() << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "canvas_poc01_golden: " << error.what() << '\n';
    return 1;
  }
}
