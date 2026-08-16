#include "runner_support.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "apple_metal_adapter.h"
#include "canvas_poc/canvas_poc.h"
#include "foundation.h"
#include "platform_bridge_internal.h"

namespace canvas::poc01 {
namespace {

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open " + path.string());
  }
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

std::string ReadText(const std::filesystem::path& path) {
  const std::vector<uint8_t> bytes = ReadBytes(path);
  return std::string(bytes.begin(), bytes.end());
}

std::string LastError() {
  size_t required = 0;
  canvas_poc_last_error(nullptr, 0, &required);
  std::vector<char> value(required);
  if (canvas_poc_last_error(value.data(), value.size(), &required) !=
      CANVAS_POC_STATUS_OK) {
    return "unknown error";
  }
  return value.data();
}

void Check(canvas_poc_status_t status, const char* action) {
  if (status != CANVAS_POC_STATUS_OK) {
    throw std::runtime_error(std::string(action) + ": " + LastError());
  }
}

struct Session {
  canvas_poc_handle_t runtime = 0;
  canvas_poc_handle_t document = 0;

  ~Session() {
    if (document != 0) canvas_poc_document_destroy(document);
    if (runtime != 0) canvas_poc_runtime_destroy(runtime);
  }
};

std::unique_ptr<Session> CreateSession(
    const std::filesystem::path& fixture_directory,
    const std::filesystem::path& font_path,
    bool add_smoke_nodes) {
  auto session = std::make_unique<Session>();
  canvas_poc_runtime_config_v1 runtime_config{};
  runtime_config.struct_size = sizeof(runtime_config);
  runtime_config.abi_version = CANVAS_POC_ABI_VERSION;
  Check(canvas_poc_runtime_create(&runtime_config, &session->runtime),
        "runtime create");

  const std::vector<uint8_t> checker =
      ReadBytes(fixture_directory / "checker.png");
  const std::vector<uint8_t> font = ReadBytes(font_path);
  Check(canvas_poc_runtime_register_asset(session->runtime, "checker.png", 11,
                                          checker.data(), checker.size()),
        "checker registration");
  Check(canvas_poc_runtime_register_asset(session->runtime, "roboto.ttf", 10,
                                          font.data(), font.size()),
        "font registration");

  canvas_poc_document_config_v1 document_config{};
  document_config.struct_size = sizeof(document_config);
  document_config.abi_version = CANVAS_POC_ABI_VERSION;
  document_config.page_width = 800;
  document_config.page_height = 600;
  document_config.background_rgba[0] = 244;
  document_config.background_rgba[1] = 245;
  document_config.background_rgba[2] = 247;
  document_config.background_rgba[3] = 255;
  Check(canvas_poc_document_create(session->runtime, &document_config,
                                   &session->document),
        "document create");
  const std::string replay = ReadText(fixture_directory / "scene.ndjson");
  Check(canvas_poc_document_apply_ndjson(session->document, replay.data(),
                                         replay.size()),
        "fixture replay");
  if (add_smoke_nodes) {
    std::ostringstream generated;
    uint64_t sequence = 8;
    for (uint64_t id = 1000; id < 1996; ++id, ++sequence) {
      const uint64_t index = id - 1000;
      generated << "{\"v\":1,\"seq\":" << sequence
                << ",\"op\":\"create\",\"node\":{\"id\":" << id
                << ",\"type\":\"rect\",\"order\":" << (100 + index)
                << ",\"x\":" << (index % 40U) * 20U
                << ",\"y\":" << (index / 40U) * 20U
                << ",\"width\":12,\"height\":12,"
                   "\"color\":[64,120,220,96]}}\n";
    }
    const std::string operations = generated.str();
    Check(canvas_poc_document_apply_ndjson(
              session->document, operations.data(), operations.size()),
          "smoke scene replay");
  }
  return session;
}

std::string Digest(canvas_poc_handle_t document) {
  char digest[33]{};
  size_t digest_size = 0;
  Check(canvas_poc_document_digest(document, digest, sizeof(digest),
                                   &digest_size),
        "document digest");
  return digest;
}

void WriteRgba(const std::filesystem::path& path,
               const std::vector<uint8_t>& rgba) {
  if (path.empty()) return;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("failed to create Apple RGBA artifact");
  }
  output.write(reinterpret_cast<const char*>(rgba.data()),
               static_cast<std::streamsize>(rgba.size()));
}

}  // namespace

std::string RunAppleAcceptance(
    const std::filesystem::path& fixture_directory,
    const std::filesystem::path& font_path,
    const std::filesystem::path& rgba_output,
    std::string platform,
    int lifecycle_iterations,
    int smoke_seconds) {
  if (lifecycle_iterations <= 0 || smoke_seconds < 0) {
    throw std::runtime_error("Apple acceptance counts are invalid");
  }
  std::string digest;
  std::vector<uint8_t> pixels;
  std::string device_name;
  for (int iteration = 0; iteration < lifecycle_iterations; ++iteration) {
    std::unique_ptr<Session> session =
        CreateSession(fixture_directory, font_path, false);
    const std::string current = Digest(session->document);
    if (iteration == 0) {
      digest = current;
    } else if (current != digest) {
      throw std::runtime_error("Apple digest changed across lifecycle runs");
    }
    if (digest != "47826449b895ac4f4a57b4f386379775") {
      throw std::runtime_error("Apple digest differs from reviewed fixture");
    }
    std::shared_ptr<Document> document =
        ResolveDocumentForPlatform(session->document);
    AppleMetalAdapter adapter;
    Check(adapter.Initialize(800, 600), "Metal initialize");
    Check(adapter.Render(*document, &pixels), "Metal render");
    device_name = adapter.device_name();
  }
  WriteRgba(rgba_output, pixels);
  const std::string reference_pixels_hash = HashHex(HashBytes(pixels));

  uint64_t smoke_frames = 0;
  double max_frame_ms = 0.0;
  if (smoke_seconds > 0) {
    std::unique_ptr<Session> smoke =
        CreateSession(fixture_directory, font_path, true);
    std::shared_ptr<Document> document =
        ResolveDocumentForPlatform(smoke->document);
    AppleMetalAdapter adapter;
    Check(adapter.Initialize(800, 600), "Metal smoke initialize");
    for (int warmup = 0; warmup < 60; ++warmup) {
      Check(adapter.Render(*document, &pixels), "Metal smoke warmup");
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(smoke_seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      const auto start = std::chrono::steady_clock::now();
      Check(adapter.Render(*document, &pixels), "Metal smoke render");
      const double frame_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start)
                                  .count();
      max_frame_ms = std::max(max_frame_ms, frame_ms);
      ++smoke_frames;
    }
    if (max_frame_ms > 100.0) {
      throw std::runtime_error("Apple Metal smoke frame exceeded 100 ms: " +
                               std::to_string(max_frame_ms));
    }
  }
  std::ostringstream result;
  result << "{\"platform\":\"" << platform
         << "\",\"backend\":\"ganesh-metal\","
            "\"digest\":\""
         << digest << "\",\"pixel_hash\":\"" << reference_pixels_hash
         << "\",\"device\":\"" << device_name
         << "\",\"lifecycle\":" << lifecycle_iterations
         << ",\"smoke_seconds\":" << smoke_seconds
         << ",\"smoke_frames\":" << smoke_frames
         << ",\"max_frame_ms\":" << max_frame_ms << "}";
  return result.str();
}

std::string RunAppleFixture(const std::filesystem::path& fixture_directory,
                            const std::filesystem::path& font_path,
                            const std::filesystem::path& rgba_output,
                            std::string platform) {
  return RunAppleAcceptance(fixture_directory, font_path, rgba_output,
                            std::move(platform), 1, 0);
}

}  // namespace canvas::poc01
