#include "runner_support.h"

#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <CoreGraphics/CoreGraphics.h>
#endif
#include <mach/mach.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "apple_metal_adapter.h"
#include "canvas_poc/canvas_poc.h"
#include "conformance.h"
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

uint64_t ProcessFootprintBytes() {
  task_vm_info_data_t info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  const kern_return_t status = task_info(
      mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info),
      &count);
  if (status != KERN_SUCCESS) {
    throw std::runtime_error("task_info(TASK_VM_INFO) failed");
  }
  return static_cast<uint64_t>(info.phys_footprint);
}

std::string ThermalState() {
  switch (NSProcessInfo.processInfo.thermalState) {
    case NSProcessInfoThermalStateNominal:
      return "nominal";
    case NSProcessInfoThermalStateFair:
      return "fair";
    case NSProcessInfoThermalStateSerious:
      return "serious";
    case NSProcessInfoThermalStateCritical:
      return "critical";
  }
  return "unknown";
}

std::string EscapeJson(std::string_view value) {
  std::ostringstream escaped;
  for (const unsigned char character : value) {
    switch (character) {
      case '\\':
        escaped << "\\\\";
        break;
      case '"':
        escaped << "\\\"";
        break;
      case '\b':
        escaped << "\\b";
        break;
      case '\f':
        escaped << "\\f";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (character < 0x20U) {
          constexpr char kHex[] = "0123456789abcdef";
          escaped << "\\u00" << kHex[character >> 4U]
                  << kHex[character & 0x0FU];
        } else {
          escaped << static_cast<char>(character);
        }
    }
  }
  return escaped.str();
}

std::optional<double> MaximumRefreshRateHz() {
#if TARGET_OS_IPHONE
  return static_cast<double>(UIScreen.mainScreen.maximumFramesPerSecond);
#else
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(CGMainDisplayID());
  if (mode == nullptr) return std::nullopt;
  const double refresh_rate = CGDisplayModeGetRefreshRate(mode);
  CGDisplayModeRelease(mode);
  if (refresh_rate <= 0.0) return std::nullopt;
  return refresh_rate;
#endif
}

}  // namespace

std::string RunAppleAcceptance(
    const std::filesystem::path& fixture_directory,
    const std::filesystem::path& font_path,
    const std::filesystem::path& rgba_output,
    std::string platform,
    int lifecycle_iterations,
    int smoke_seconds,
    const std::function<void(bool)>& smoke_phase) {
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
  std::vector<std::pair<int64_t, uint64_t>> memory_samples;
  const std::string thermal_state_before = ThermalState();
  const bool low_power_mode = NSProcessInfo.processInfo.lowPowerModeEnabled;
  const std::optional<double> maximum_refresh_hz = MaximumRefreshRateHz();
  if (smoke_seconds > 0) {
    std::unique_ptr<Session> smoke =
        CreateSession(fixture_directory, font_path, true);
    std::shared_ptr<Document> document =
        ResolveDocumentForPlatform(smoke->document);
    AppleMetalAdapter adapter;
    Check(adapter.Initialize(800, 600), "Metal smoke initialize");
    for (int warmup = 0; warmup < 60; ++warmup) {
      Check(adapter.Render(*document), "Metal smoke warmup");
    }
    std::vector<uint8_t> warmup_drain;
    Check(adapter.Render(*document, &warmup_drain),
          "Metal smoke warmup drain");
    if (smoke_phase) smoke_phase(true);
    const auto smoke_started = std::chrono::steady_clock::now();
    const auto deadline = smoke_started + std::chrono::seconds(smoke_seconds);
    auto next_memory_sample = smoke_started + std::chrono::seconds(5);
    memory_samples.emplace_back(0, ProcessFootprintBytes());
    const auto frame_interval = std::chrono::microseconds(16667);
    auto next_frame = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_until(next_frame);
      if (std::chrono::steady_clock::now() >= deadline) break;
      const auto start = std::chrono::steady_clock::now();
      Check(adapter.Render(*document), "Metal smoke render");
      const auto completed = std::chrono::steady_clock::now();
      const double frame_ms =
          std::chrono::duration<double, std::milli>(completed - start).count();
      max_frame_ms = std::max(max_frame_ms, frame_ms);
      ++smoke_frames;
      if (completed >= next_memory_sample) {
        const int64_t elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                completed - smoke_started)
                .count();
        memory_samples.emplace_back(elapsed_ms, ProcessFootprintBytes());
        do {
          next_memory_sample += std::chrono::seconds(5);
        } while (next_memory_sample <= completed);
      }
      next_frame = std::max(next_frame + frame_interval, completed);
    }
    const auto smoke_completed = std::chrono::steady_clock::now();
    const int64_t completed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(smoke_completed -
                                                               smoke_started)
            .count();
    if (memory_samples.empty() ||
        completed_ms - memory_samples.back().first >= 1000) {
      memory_samples.emplace_back(completed_ms, ProcessFootprintBytes());
    }
    if (smoke_phase) smoke_phase(false);
    std::vector<uint8_t> smoke_drain;
    Check(adapter.Render(*document, &smoke_drain), "Metal smoke drain");
    if (max_frame_ms > 100.0) {
      throw std::runtime_error("Apple Metal smoke frame exceeded 100 ms: " +
                               std::to_string(max_frame_ms));
    }
  }
  const std::string thermal_state_after = ThermalState();
  const CoreConformanceResult conformance = RunCoreConformance();
  std::ostringstream result;
  result << "{\"platform\":\"" << platform
         << "\",\"backend\":\"ganesh-metal\","
            "\"digest\":\""
         << digest << "\",\"pixel_hash\":\"" << reference_pixels_hash
         << "\",\"device\":\"" << EscapeJson(device_name)
         << "\",\"lifecycle\":" << lifecycle_iterations
         << ",\"smoke_seconds\":" << smoke_seconds
         << ",\"smoke_frames\":" << smoke_frames
         << ",\"max_frame_ms\":" << max_frame_ms
         << ",\"memory_scope\":\"process-physical-footprint\","
            "\"memory_sampling_interval_ms\":5000,\"memory_samples\":[";
  for (size_t index = 0; index < memory_samples.size(); ++index) {
    if (index != 0) result << ',';
    result << "{\"elapsed_ms\":" << memory_samples[index].first
           << ",\"bytes\":" << memory_samples[index].second << '}';
  }
  result << "],\"performance_environment\":{\"thermal_state_before\":\""
         << thermal_state_before << "\",\"thermal_state_after\":\""
         << thermal_state_after << "\",\"low_power_mode\":"
         << (low_power_mode ? "true" : "false")
         << ",\"power_observation_method\":\"NSProcessInfo.lowPowerModeEnabled\","
            "\"display_refresh\":{";
  if (maximum_refresh_hz) {
    result << "\"maximum_hz\":" << *maximum_refresh_hz
           << ",\"availability\":\"maximum-only\"";
  } else {
    result << "\"maximum_hz\":null,\"availability\":\"unavailable\"";
  }
  result << ",\"observation_method\":\"platform display API\"},"
            "\"target_frame_interval_ms\":16.667,"
            "\"vrr\":{\"status\":\"unavailable\","
            "\"observation_method\":\"no public API exposes active VRR state\"},"
            "\"browser_throttling\":{\"status\":\"not_applicable\","
            "\"observation_method\":\"native Metal runner\"}},"
         << CoreConformanceJsonFields(conformance) << "}";
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
