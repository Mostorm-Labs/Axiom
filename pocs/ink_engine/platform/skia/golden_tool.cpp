#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "canvas_poc02/ink_engine.h"
#include "ink_skia_renderer.h"

namespace fs = std::filesystem;

namespace {

std::string ReadText(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot open " + path.string());
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

void Write(const fs::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) throw std::runtime_error("cannot write " + path.string());
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main(int argc, char** argv) {
  bool update = false;
  fs::path output;
  std::string fixture = "vector-pressure.ndjson";
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--update-golden") update = true;
    else if (argument.starts_with("--output=")) output = argument.substr(9);
    else if (argument.starts_with("--fixture=")) fixture = argument.substr(10);
    else return 2;
  }
  if (output.empty()) {
    output = fs::path(CANVAS_POC02_GOLDEN_DIR) /
             (fixture.starts_with("dab") ? "dab-reference.rgba"
                                         : "vector-reference.rgba");
  }
  if (!update && output.parent_path() == fs::path(CANVAS_POC02_GOLDEN_DIR)) {
    std::cerr << "reviewed baselines require explicit --update-golden\n";
    return 2;
  }
  try {
    canvas::poc02::ReplayFixture replay;
    std::string error;
    const auto status = canvas::poc02::ParseReplayFixture(
        ReadText(fs::path(CANVAS_POC02_FIXTURE_DIR) / fixture), &replay, &error);
    if (status != canvas::poc02::Status::kOk) throw std::runtime_error(error);
    canvas::poc02::StrokeDocument document;
    canvas::poc02::DefaultPreviewSink sink;
    canvas::poc02::AddStrokeOperation operation;
    if (canvas::poc02::RunReplayFixture(replay, &document, &sink, &operation,
                                        &error) != canvas::poc02::Status::kOk) {
      throw std::runtime_error(error);
    }
    std::vector<uint8_t> pixels;
    if (!canvas::poc02::InkSkiaRenderer().RenderRaster(
            800, 600, document, nullptr, &pixels)) {
      throw std::runtime_error("Skia raster render failed");
    }
    Write(output, pixels);
    std::cout << "{\"fixture\":\"" << fixture << "\",\"stroke_digest\":\""
              << canvas::poc02::StrokeDigest(operation.stroke)
              << "\",\"document_digest\":\"" << document.Digest()
              << "\",\"bytes\":" << pixels.size() << "}\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << "\n";
    return 1;
  }
}
