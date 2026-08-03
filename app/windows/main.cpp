#include "whiteboard_app.h"

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::optional<std::string> wideToUtf8(std::wstring_view value) {
  if (value.empty() || value.size() > static_cast<std::size_t>(
                           (std::numeric_limits<int>::max)())) {
    return std::nullopt;
  }
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (length <= 0) return std::nullopt;
  std::string result(static_cast<std::size_t>(length), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), length,
                          nullptr, nullptr) != length) return std::nullopt;
  return result;
}

bool isSessionToken(std::string_view token) {
  return token.size() == 64U &&
         std::all_of(token.begin(), token.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

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
    } else if (argument == L"--ipc-pipe" && index + 1 < argumentCount) {
      options.ipcPipe = arguments[++index];
    } else if (argument == L"--session-token" && index + 1 < argumentCount) {
      const auto token = wideToUtf8(arguments[++index]);
      if (!token || !isSessionToken(*token)) {
        valid = false;
        break;
      }
      options.sessionToken = *token;
    } else {
      valid = false;
      break;
    }
  }
  if (options.ipcPipe.has_value() != options.sessionToken.has_value() ||
      (options.ipcPipe && options.ipcPipe->empty())) valid = false;
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
