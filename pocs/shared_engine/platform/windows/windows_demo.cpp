#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "canvas_poc/canvas_poc.h"
#include "foundation.h"
#include "platform_bridge_internal.h"
#include "windows_d3d12_adapter.h"

namespace {

namespace fs = std::filesystem;

struct Options {
  bool offscreen = false;
  bool hardware = false;
  int lifecycle = 1;
  int smoke_seconds = 0;
  fs::path output;
};

std::vector<uint8_t> ReadBytes(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("failed to open fixture");
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

std::string ReadText(const fs::path& path) {
  const auto bytes = ReadBytes(path);
  return std::string(bytes.begin(), bytes.end());
}

std::string LastError() {
  size_t required = 0;
  canvas_poc_last_error(nullptr, 0, &required);
  std::vector<char> message(required);
  canvas_poc_last_error(message.data(), message.size(), &required);
  return message.empty() ? "unknown" : message.data();
}

void Check(canvas_poc_status_t status, const char* action) {
  if (status != CANVAS_POC_STATUS_OK) {
    throw std::runtime_error(std::string(action) + ": " + LastError());
  }
}

Options Parse() {
  Options options;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int index = 1; index < argc; ++index) {
    std::wstring argument(argv[index]);
    if (argument == L"--offscreen") options.offscreen = true;
    else if (argument == L"--hardware") options.hardware = true;
    else if (argument.starts_with(L"--lifecycle="))
      options.lifecycle = std::stoi(argument.substr(12));
    else if (argument.starts_with(L"--smoke="))
      options.smoke_seconds = std::stoi(argument.substr(8));
    else if (argument.starts_with(L"--output="))
      options.output = argument.substr(9);
    else throw std::runtime_error("unknown Windows demo argument");
  }
  LocalFree(argv);
  if (options.lifecycle <= 0 || options.smoke_seconds < 0) {
    throw std::runtime_error("lifecycle must be positive and smoke non-negative");
  }
  return options;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

HWND CreateDemoWindow() {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"CanvasPoc01Window";
  RegisterClassW(&window_class);
  HWND window = CreateWindowExW(0, window_class.lpszClassName,
                                L"Canvas POC-01 D3D12", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 816, 639, nullptr,
                                nullptr, window_class.hInstance, nullptr);
  ShowWindow(window, SW_SHOW);
  return window;
}

struct Session {
  canvas_poc_handle_t runtime = 0;
  canvas_poc_handle_t document = 0;
  Session() = default;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&& other) noexcept
      : runtime(other.runtime), document(other.document) {
    other.runtime = 0;
    other.document = 0;
  }
  ~Session() {
    if (document) canvas_poc_document_destroy(document);
    if (runtime) canvas_poc_runtime_destroy(runtime);
  }
};

Session LoadFixture(bool add_smoke_nodes = false) {
  Session session;
  canvas_poc_runtime_config_v1 runtime_config{};
  runtime_config.struct_size = sizeof(runtime_config);
  runtime_config.abi_version = CANVAS_POC_ABI_VERSION;
  Check(canvas_poc_runtime_create(&runtime_config, &session.runtime), "runtime");
  const auto checker = ReadBytes(fs::path(CANVAS_POC01_FIXTURE_DIR) / L"checker.png");
  const auto font = ReadBytes(CANVAS_POC01_FONT_PATH);
  Check(canvas_poc_runtime_register_asset(session.runtime, "checker.png", 11,
                                          checker.data(), checker.size()),
        "checker");
  Check(canvas_poc_runtime_register_asset(session.runtime, "roboto.ttf", 10,
                                          font.data(), font.size()),
        "font");
  canvas_poc_document_config_v1 config{};
  config.struct_size = sizeof(config);
  config.abi_version = CANVAS_POC_ABI_VERSION;
  config.page_width = 800;
  config.page_height = 600;
  config.background_rgba[0] = 244;
  config.background_rgba[1] = 245;
  config.background_rgba[2] = 247;
  config.background_rgba[3] = 255;
  Check(canvas_poc_document_create(session.runtime, &config, &session.document),
        "document");
  const std::string replay =
      ReadText(fs::path(CANVAS_POC01_FIXTURE_DIR) / L"scene.ndjson");
  Check(canvas_poc_document_apply_ndjson(session.document, replay.data(),
                                         replay.size()),
        "replay");
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
                << ",\"width\":12,\"height\":12,\"color\":[64,120,220,96]}}\n";
    }
    const std::string operations = generated.str();
    Check(canvas_poc_document_apply_ndjson(
              session.document, operations.data(), operations.size()),
          "smoke scene");
  }
  return session;
}

std::string Digest(canvas_poc_handle_t document) {
  char digest[33]{};
  size_t required = 0;
  Check(canvas_poc_document_digest(document, digest, sizeof(digest), &required),
        "digest");
  return digest;
}

}  // namespace

int wmain() {
  try {
    const Options options = Parse();
    HWND window = options.offscreen ? nullptr : CreateDemoWindow();
    std::string digest;
    std::vector<uint8_t> pixels;
    canvas::poc01::WindowsAdapterInfo adapter_info;
    for (int iteration = 0; iteration < options.lifecycle; ++iteration) {
      Session session = LoadFixture();
      const std::string current = Digest(session.document);
      if (iteration == 0) digest = current;
      else if (current != digest) throw std::runtime_error("digest instability");
      auto internal = canvas::poc01::ResolveDocumentForPlatform(session.document);
      canvas::poc01::WindowsD3d12Adapter adapter;
      Check(adapter.Initialize(window, !options.hardware, 800, 600), "D3D12 init");
      Check(adapter.Render(*internal, &pixels), "D3D12 render");
      Check(adapter.PresentToWindow(pixels), "window present");
      adapter_info = adapter.info();
    }
    if (!options.output.empty()) {
      std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
      output.write(reinterpret_cast<const char*>(pixels.data()),
                   static_cast<std::streamsize>(pixels.size()));
    }
    uint64_t smoke_frames = 0;
    double max_frame_ms = 0;
    if (options.smoke_seconds > 0) {
      Session session = LoadFixture(true);
      auto internal = canvas::poc01::ResolveDocumentForPlatform(session.document);
      canvas::poc01::WindowsD3d12Adapter adapter;
      Check(adapter.Initialize(nullptr, !options.hardware, 800, 600), "smoke init");
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(options.smoke_seconds);
      while (std::chrono::steady_clock::now() < deadline) {
        const auto start = std::chrono::steady_clock::now();
        Check(adapter.Render(*internal, &pixels), "smoke render");
        const double frame_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
        max_frame_ms = (std::max)(max_frame_ms, frame_ms);
        ++smoke_frames;
      }
      if (max_frame_ms > 100.0) throw std::runtime_error("frame >100ms");
    }
    std::ostringstream result;
    result << "{\"platform\":\"windows\",\"backend\":\"ganesh-d3d12\","
              "\"digest\":\""
           << digest << "\",\"warp\":" << (adapter_info.warp ? "true" : "false")
           << ",\"vendor_id\":" << adapter_info.vendor_id
           << ",\"device_id\":" << adapter_info.device_id
           << ",\"driver\":" << adapter_info.driver_version
           << ",\"lifecycle\":" << options.lifecycle
           << ",\"smoke_seconds\":" << options.smoke_seconds
           << ",\"smoke_frames\":" << smoke_frames
           << ",\"max_frame_ms\":" << max_frame_ms << "}";
    std::cout << result.str() << '\n';
    if (window != nullptr) {
      MSG message{};
      while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "canvas_poc01_windows: " << error.what() << '\n';
    return 1;
  }
}
