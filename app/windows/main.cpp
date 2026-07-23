#include "whiteboard_app.h"

#include <objbase.h>
#include <shellapi.h>

#include <optional>
#include <string_view>
#include <utility>

namespace {

std::optional<canvas::windows::WhiteboardRunOptions> parseOptions() {
  int argumentCount = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
  if (arguments == nullptr) return std::nullopt;

  canvas::windows::WhiteboardRunOptions options;
  bool valid = true;
  for (int index = 1; index < argumentCount; ++index) {
    const std::wstring_view argument(arguments[index]);
    if (argument == L"--self-test-layers") {
      options.selfTestLayers = true;
    } else if (argument == L"--self-test-embedded") {
      options.selfTestEmbedded = true;
    } else if (argument == L"--self-test-document") {
      options.selfTestDocument = true;
    } else if (argument == L"--open" && index + 1 < argumentCount) {
      options.openPath = arguments[++index];
    } else if (argument == L"--save" && index + 1 < argumentCount) {
      options.savePath = arguments[++index];
    } else if (argument == L"--video" && index + 1 < argumentCount) {
      options.videoPath = arguments[++index];
    } else {
      valid = false;
      break;
    }
  }
  LocalFree(arguments);
  return valid ? std::optional<canvas::windows::WhiteboardRunOptions>(
                     std::move(options))
               : std::nullopt;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR,
                    int commandShow) {
  const HRESULT initResult =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initResult)) {
    return static_cast<int>(initResult);
  }

  int result = 0;
  {
    const auto options = parseOptions();
    if (!options) {
      CoUninitialize();
      return static_cast<int>(E_INVALIDARG);
    }
    canvas::windows::WhiteboardApp app;
    result = app.run(instance, commandShow, *options);
  }
  CoUninitialize();
  return result;
}
