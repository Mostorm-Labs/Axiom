#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "canvas_poc02/ink_engine.h"

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot open fixture: " + path);
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

}  // namespace

int main(int argc, char** argv) {
  std::string fixture_path =
      std::string(CANVAS_POC02_FIXTURE_DIR) + "/vector-pressure.ndjson";
  int repeat = 1;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument.starts_with("--fixture=")) fixture_path = argument.substr(10);
    else if (argument.starts_with("--repeat=")) repeat = std::stoi(argument.substr(9));
    else {
      std::cerr << "unknown argument: " << argument << "\n";
      return 2;
    }
  }
  try {
    canvas::poc02::ReplayFixture fixture;
    std::string error;
    const auto parse_status = canvas::poc02::ParseReplayFixture(
        ReadFile(fixture_path), &fixture, &error);
    if (parse_status != canvas::poc02::Status::kOk) {
      std::cerr << "parse failed: " << error << "\n";
      return 1;
    }
    std::string expected_document_digest;
    std::string expected_stroke_digest;
    std::string expected_preview_digest;
    double total_milliseconds = 0.0;
    for (int run = 0; run < repeat; ++run) {
      canvas::poc02::StrokeDocument document;
      canvas::poc02::DefaultPreviewSink sink;
      canvas::poc02::AddStrokeOperation operation;
      const auto started = std::chrono::steady_clock::now();
      const auto status = canvas::poc02::RunReplayFixture(
          fixture, &document, &sink, &operation, &error);
      const auto finished = std::chrono::steady_clock::now();
      total_milliseconds +=
          std::chrono::duration<double, std::milli>(finished - started).count();
      if (status != canvas::poc02::Status::kOk) {
        std::cerr << "replay failed: " << error << "\n";
        return 1;
      }
      canvas::poc02::StrokeDocument replayed;
      const std::string ndjson = canvas::poc02::SerializeAddStrokeNdjson(operation);
      canvas::poc02::AddStrokeOperation decoded;
      if (canvas::poc02::ParseAddStrokeNdjson(ndjson, &decoded, &error) !=
              canvas::poc02::Status::kOk ||
          replayed.Apply(decoded) != canvas::poc02::Status::kOk ||
          replayed.Digest() != document.Digest()) {
        std::cerr << "operation replay did not reproduce the document\n";
        return 1;
      }
      const std::string document_digest = document.Digest();
      const std::string stroke_digest = canvas::poc02::StrokeDigest(operation.stroke);
      const std::string preview_digest = sink.ModelDigest();
      if (run == 0) {
        expected_document_digest = document_digest;
        expected_stroke_digest = stroke_digest;
        expected_preview_digest = preview_digest;
      } else if (document_digest != expected_document_digest ||
                 stroke_digest != expected_stroke_digest ||
                 preview_digest != expected_preview_digest) {
        std::cerr << "repeat replay was not deterministic\n";
        return 1;
      }
    }
    std::cout << "{\"schema\":1,\"fixture\":\"" << fixture_path
              << "\",\"document_digest\":\"" << expected_document_digest
              << "\",\"stroke_digest\":\"" << expected_stroke_digest
              << "\",\"preview_digest\":\"" << expected_preview_digest
              << "\",\"numeric_digest\":\""
              << canvas::poc02::NumericConformanceDigest()
              << "\",\"repeat\":" << repeat
              << ",\"average_replay_ms\":" << (total_milliseconds / repeat)
              << "}\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << "\n";
    return 1;
  }
}
