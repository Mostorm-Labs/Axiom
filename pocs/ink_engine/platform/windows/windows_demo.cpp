#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "canvas_poc02/ink_engine.h"
#include "ink_skia_renderer.h"
#include "windows_d3d12_adapter.h"

namespace fs = std::filesystem;

namespace {

struct Options {
  bool offscreen = false;
  bool hardware = false;
  int lifecycle = 1;
  fs::path output;
  fs::path result;
};

std::string ReadText(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("failed to open fixture");
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("failed to open golden");
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

void Check(canvas::poc02::Status status, const char* action,
           const canvas::poc02::WindowsD3d12Adapter* adapter = nullptr) {
  if (status == canvas::poc02::Status::kOk) return;
  const std::string detail = adapter == nullptr ? std::string()
                                                : ": " + adapter->error();
  throw std::runtime_error(std::string(action) + " failed: " +
                           std::string(canvas::poc02::StatusName(status)) + detail);
}

Options Parse() {
  Options options;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int index = 1; index < argc; ++index) {
    const std::wstring argument(argv[index]);
    if (argument == L"--offscreen") options.offscreen = true;
    else if (argument == L"--hardware") options.hardware = true;
    else if (argument.starts_with(L"--lifecycle="))
      options.lifecycle = std::stoi(argument.substr(12));
    else if (argument.starts_with(L"--output="))
      options.output = argument.substr(9);
    else if (argument.starts_with(L"--result="))
      options.result = argument.substr(9);
    else throw std::runtime_error("unknown Windows demo argument");
  }
  LocalFree(argv);
  if (options.lifecycle <= 0) throw std::runtime_error("lifecycle must be positive");
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

HWND CreateWindowForDemo() {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"CanvasPoc02Window";
  RegisterClassW(&window_class);
  HWND window = CreateWindowExW(
      0, window_class.lpszClassName, L"Canvas POC-02 Ink Engine",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 816, 639, nullptr,
      nullptr, window_class.hInstance, nullptr);
  ShowWindow(window, SW_SHOW);
  return window;
}

struct ReplayResult {
  canvas::poc02::StrokeDocument document;
  canvas::poc02::DefaultPreviewSink preview;
  canvas::poc02::AddStrokeOperation operation;
};

ReplayResult Replay(const wchar_t* fixture_name) {
  canvas::poc02::ReplayFixture fixture;
  std::string error;
  Check(canvas::poc02::ParseReplayFixture(
            ReadText(fs::path(CANVAS_POC02_FIXTURE_DIR) / fixture_name),
            &fixture, &error), "parse fixture");
  ReplayResult result;
  Check(canvas::poc02::RunReplayFixture(fixture, &result.document,
                                        &result.preview, &result.operation,
                                        &error), "run fixture");
  return result;
}

void Write(const fs::path& path, std::span<const uint8_t> bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) throw std::runtime_error("failed to write output");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int wmain() {
  try {
    const Options options = Parse();
    HWND window = options.offscreen ? nullptr : CreateWindowForDemo();
    std::string expected_document_digest;
    std::string expected_stroke_digest;
    std::vector<uint8_t> pixels;
    canvas::poc02::WindowsAdapterInfo adapter_info;
    for (int iteration = 0; iteration < options.lifecycle; ++iteration) {
      ReplayResult result = Replay(L"vector-pressure.ndjson");
      const std::string document_digest = result.document.Digest();
      const std::string stroke_digest =
          canvas::poc02::StrokeDigest(result.operation.stroke);
      if (iteration == 0) {
        expected_document_digest = document_digest;
        expected_stroke_digest = stroke_digest;
      } else if (document_digest != expected_document_digest ||
                 stroke_digest != expected_stroke_digest) {
        throw std::runtime_error("digest changed across lifecycle iterations");
      }
      canvas::poc02::WindowsD3d12Adapter adapter;
      Check(adapter.Initialize(window, !options.hardware, 800, 600),
            "D3D12 initialize", &adapter);
      Check(adapter.Render(result.document, nullptr, &pixels), "D3D12 render",
            &adapter);
      Check(adapter.PresentToWindow(pixels), "window present", &adapter);
      adapter_info = adapter.info();
    }
    const auto golden = ReadBytes(fs::path(CANVAS_POC02_GOLDEN_DIR) /
                                  L"vector-reference.rgba");
    const canvas::poc02::PixelMetrics metrics =
        canvas::poc02::CompareRgba(golden, pixels, 2);
    const double ratio = static_cast<double>(metrics.matching_pixels) /
                         static_cast<double>(metrics.total_pixels);
    if (ratio < 0.999 || metrics.maximum_channel_delta > 2) {
      throw std::runtime_error("Windows visual gate failed");
    }
    ReplayResult dab = Replay(L"dab-turn.ndjson");
    std::vector<uint8_t> dab_pixels;
    canvas::poc02::WindowsD3d12Adapter dab_adapter;
    Check(dab_adapter.Initialize(nullptr, !options.hardware, 800, 600),
          "D3D12 Dab initialize", &dab_adapter);
    Check(dab_adapter.Render(dab.document, nullptr, &dab_pixels),
          "D3D12 Dab render", &dab_adapter);
    const auto dab_golden = ReadBytes(fs::path(CANVAS_POC02_GOLDEN_DIR) /
                                      L"dab-reference.rgba");
    const canvas::poc02::PixelMetrics dab_metrics =
        canvas::poc02::CompareRgba(dab_golden, dab_pixels, 2);
    const double dab_ratio = static_cast<double>(dab_metrics.matching_pixels) /
                             static_cast<double>(dab_metrics.total_pixels);
    if (dab_ratio < 0.999 || dab_metrics.maximum_channel_delta > 2) {
      throw std::runtime_error("Windows Dab visual gate failed");
    }
    if (!options.output.empty()) Write(options.output, pixels);
    std::ostringstream json;
    json << "{\"platform\":\"windows\",\"backend\":\"ganesh-d3d12-"
         << (adapter_info.warp ? "warp" : "hardware")
         << "\",\"document_digest\":\"" << expected_document_digest
         << "\",\"stroke_digest\":\"" << expected_stroke_digest
         << "\",\"preview_digest\":\""
         << Replay(L"vector-pressure.ndjson").preview.ModelDigest()
         << "\",\"numeric_digest\":\""
         << canvas::poc02::NumericConformanceDigest()
         << "\",\"lifecycle\":" << options.lifecycle
         << ",\"matching_ratio\":" << ratio
         << ",\"maximum_channel_delta\":"
         << static_cast<uint32_t>(metrics.maximum_channel_delta)
         << ",\"dab_document_digest\":\"" << dab.document.Digest()
         << "\",\"dab_stroke_digest\":\""
         << canvas::poc02::StrokeDigest(dab.operation.stroke)
         << "\",\"dab_preview_digest\":\"" << dab.preview.ModelDigest()
         << "\",\"dab_matching_ratio\":" << dab_ratio
         << ",\"dab_maximum_channel_delta\":"
         << static_cast<uint32_t>(dab_metrics.maximum_channel_delta)
         << ",\"adapter\":\"" << adapter_info.description
         << "\",\"vendor_id\":" << adapter_info.vendor_id
         << ",\"device_id\":" << adapter_info.device_id << "}";
    if (!options.result.empty()) {
      std::ofstream result_file(options.result, std::ios::trunc);
      result_file << json.str() << "\n";
    }
    std::cout << json.str() << "\n";
    if (window != nullptr) {
      MSG message{};
      while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << "\n";
    return 1;
  }
}
