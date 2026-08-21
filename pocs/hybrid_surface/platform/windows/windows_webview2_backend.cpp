#include "windows_webview2_backend.h"

#include <windows.h>

#include <wrl.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace canvas::poc05::windows {
namespace {

std::string HResultMessage(HRESULT hr, const char* action) {
  std::ostringstream stream;
  stream << action << " failed (0x" << std::hex
         << static_cast<unsigned long>(hr) << ")";
  return stream.str();
}

std::wstring HtmlFor(SurfaceKind kind, bool failed) {
  const wchar_t* label = kind == SurfaceKind::kVideo ? L"POC-05 video" :
                                                          L"POC-05 WebView2";
  const wchar_t* body = failed
                            ? L"<div class='failure'>External surface unavailable</div>"
                            : (kind == SurfaceKind::kVideo
                                   ? L"<video id='surface' autoplay muted loop controls></video>"
                                   : L"<div id='surface'>Loading native surface...</div>");
  std::wstring html =
      L"<!doctype html><meta charset='utf-8'><style>html,body{margin:0;width:100%;height:100%;overflow:hidden;background:#10233b;color:#d9e9ff;font:16px sans-serif}#surface{display:grid;place-items:center;width:100%;height:100%}.failure{display:grid;place-items:center;width:100%;height:100%;background:#5b1f26;color:#ffd8dc}</style><title>";
  html += label;
  html += L"</title>";
  html += body;
  return html;
}

struct SurfaceEntry {
  SurfaceKind kind = SurfaceKind::kWebView;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> webview;
  bool ready = false;
  bool has_content_state = false;
  bool last_failure = false;
  bool has_opacity = false;
  float last_opacity = 1.0F;
};

}  // namespace

struct WebView2OverlayBackend::Impl {
  explicit Impl(WebView2BackendOptions value) : options(std::move(value)) {}

  WebView2BackendOptions options;
  ComPtr<ICoreWebView2Environment> environment;
  std::unordered_map<ExternalSurfaceId, SurfaceEntry> surfaces;
  HANDLE initialized_event = nullptr;
  bool initialization_started = false;
  bool initialized = false;
  HRESULT initialization_error = S_OK;
  std::string runtime_version;
  bool com_initialized = false;
};

WebView2OverlayBackend::WebView2OverlayBackend(WebView2BackendOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

WebView2OverlayBackend::~WebView2OverlayBackend() {
  if (!impl_) return;
  impl_->surfaces.clear();
  impl_->environment.Reset();
  if (impl_->initialized_event != nullptr) {
    CloseHandle(impl_->initialized_event);
    impl_->initialized_event = nullptr;
  }
  if (impl_->com_initialized) CoUninitialize();
}

bool WebView2OverlayBackend::initialize(std::string* error) {
  if (error == nullptr || impl_->options.owner_window == nullptr) {
    if (error) *error = "WebView2 owner window is required";
    return false;
  }
  if (impl_->initialized) return true;
  if (impl_->initialization_started) {
    *error = FAILED(impl_->initialization_error)
                 ? HResultMessage(impl_->initialization_error,
                                  "CreateCoreWebView2EnvironmentWithOptions")
                 : "WebView2 environment is still initializing";
    return false;
  }

  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com_result)) {
    *error = HResultMessage(com_result, "CoInitializeEx");
    return false;
  }
  impl_->com_initialized = SUCCEEDED(com_result);
  impl_->initialization_started = true;
  impl_->initialized_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (impl_->initialized_event == nullptr) {
    *error = "CreateEventW failed for WebView2 initialization";
    return false;
  }
  const std::wstring user_data = impl_->options.user_data_folder.empty()
                                     ? L"poc05-webview2-user-data"
                                     : impl_->options.user_data_folder;
  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, user_data.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this](HRESULT result, ICoreWebView2Environment* environment) {
            impl_->initialization_error = result;
            if (SUCCEEDED(result) && environment != nullptr) {
              impl_->environment = environment;
              PWSTR version = nullptr;
              if (SUCCEEDED(environment->get_BrowserVersionString(&version)) &&
                  version != nullptr) {
                int required = WideCharToMultiByte(CP_UTF8, 0, version, -1,
                                                    nullptr, 0, nullptr, nullptr);
                if (required > 1) {
                  impl_->runtime_version.resize(static_cast<size_t>(required));
                  WideCharToMultiByte(CP_UTF8, 0, version, -1,
                                      impl_->runtime_version.data(), required,
                                      nullptr, nullptr);
                  impl_->runtime_version.resize(static_cast<size_t>(required - 1));
                }
                CoTaskMemFree(version);
              }
              impl_->initialized = true;
            }
            SetEvent(impl_->initialized_event);
            return S_OK;
          })
          .Get());
  if (FAILED(hr)) {
    impl_->initialization_error = hr;
    *error = HResultMessage(hr, "CreateCoreWebView2EnvironmentWithOptions");
    return false;
  }
  *error = "WebView2 environment is still initializing";
  return false;
}

bool WebView2OverlayBackend::initialized() const {
  return impl_->initialized;
}

std::string WebView2OverlayBackend::runtimeVersion() const {
  return impl_->runtime_version;
}

void WebView2OverlayBackend::runJsStallProbe(std::uint32_t milliseconds) {
  if (milliseconds == 0U) return;
  std::wstring script = L"(() => { const end = performance.now() + " +
                        std::to_wstring(milliseconds) +
                        L"; while (performance.now() < end) {} })();";
  for (const auto& [id, entry] : impl_->surfaces) {
    static_cast<void>(id);
    if (entry.webview != nullptr) entry.webview->ExecuteScript(script.c_str(), nullptr);
  }
}

bool WebView2OverlayBackend::create(ExternalSurfaceId id, SurfaceKind kind,
                                    std::string* error) {
  if (error == nullptr || id == 0U) return false;
  if (!impl_->initialized || impl_->environment == nullptr) {
    *error = "WebView2 environment is not initialized";
    return false;
  }
  if (impl_->surfaces.contains(id)) return true;

  // Controller creation is asynchronous.  The runner pumps the window queue
  // after registry creation; a component can use the same callback boundary.
  const HRESULT hr = impl_->environment->CreateCoreWebView2Controller(
      impl_->options.owner_window,
      Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
          [this, id, kind](HRESULT result, ICoreWebView2Controller* controller) {
            if (FAILED(result) || controller == nullptr) return S_OK;
            SurfaceEntry entry;
            entry.kind = kind;
            entry.controller = controller;
            controller->put_IsVisible(FALSE);
            controller->put_Bounds(RECT{0, 0, 1, 1});
            controller->get_CoreWebView2(&entry.webview);
            if (entry.webview != nullptr) {
              const std::wstring html = HtmlFor(kind, false);
              entry.webview->NavigateToString(html.c_str());
              entry.ready = true;
            }
            impl_->surfaces.insert_or_assign(id, std::move(entry));
            return S_OK;
          })
          .Get());
  if (FAILED(hr)) {
    *error = HResultMessage(hr, "CreateCoreWebView2Controller");
    return false;
  }
  // Reserve the ID so a second frame cannot issue duplicate async creates.
  impl_->surfaces.emplace(id, SurfaceEntry{kind});
  return true;
}

bool WebView2OverlayBackend::apply(const PlacementCommand& command,
                                   std::string* error) {
  if (error == nullptr) return false;
  auto found = impl_->surfaces.find(command.id);
  if (found == impl_->surfaces.end()) {
    *error = "WebView2 surface is not created";
    return false;
  }
  SurfaceEntry& entry = found->second;
  if (entry.controller == nullptr) {
    // The controller callback has not arrived yet; the registry can retry on
    // the next canonical frame without treating this as a platform failure.
    return true;
  }
  const auto clamp = [](float value) -> LONG {
    if (!std::isfinite(value)) return 0;
    return static_cast<LONG>(std::clamp(value, -32768.0F, 32767.0F));
  };
  const CanvasRectF rect = command.visible && command.relativeDeviceClip.width > 0.0F
                               ? CanvasRectF{
                                     command.deviceBounds.x +
                                         command.relativeDeviceClip.x,
                                     command.deviceBounds.y +
                                         command.relativeDeviceClip.y,
                                     command.relativeDeviceClip.width,
                                     command.relativeDeviceClip.height}
                               : command.deviceBounds;
  const UINT dpi = GetDpiForWindow(impl_->options.owner_window);
  const float scale = dpi == 0U ? 1.0F : static_cast<float>(dpi) / 96.0F;
  const RECT bounds{clamp(rect.x / scale), clamp(rect.y / scale),
                    clamp((rect.x + rect.width) / scale),
                    clamp((rect.y + rect.height) / scale)};
  HRESULT hr = entry.controller->put_Bounds(bounds);
  if (FAILED(hr)) {
    *error = HResultMessage(hr, "WebView2 put_Bounds");
    return false;
  }
  const BOOL visible = command.visible ? TRUE : FALSE;
  hr = entry.controller->put_IsVisible(visible);
  if (FAILED(hr)) {
    *error = HResultMessage(hr, "WebView2 put_IsVisible");
    return false;
  }
  if (entry.webview != nullptr && entry.ready &&
      (!entry.has_content_state || entry.last_failure != command.failurePlaceholder)) {
    const wchar_t* script = command.failurePlaceholder
                                ? L"document.body.innerHTML='<div class=\\\"failure\\\">External surface unavailable</div>';"
                                : (entry.kind == SurfaceKind::kVideo
                                       ? L"document.body.innerHTML='<video id=\\\"surface\\\" autoplay muted loop controls></video>';"
                                       : L"document.body.innerHTML='<div id=\\\"surface\\\">Native WebView2 surface ready</div>';" );
    // Keep the visual state deterministic without waiting for a JS callback.
    entry.webview->ExecuteScript(script, nullptr);
    entry.last_failure = command.failurePlaceholder;
    entry.has_content_state = true;
  }
  if (entry.webview != nullptr && entry.ready &&
      (!entry.has_opacity || std::fabs(entry.last_opacity - command.opacity) > 0.0001F)) {
    std::wstring script = L"document.documentElement.style.opacity=" +
                          std::to_wstring(std::clamp(command.opacity, 0.0F, 1.0F)) +
                          L";";
    entry.webview->ExecuteScript(script.c_str(), nullptr);
    entry.last_opacity = command.opacity;
    entry.has_opacity = true;
  }
  return true;
}

void WebView2OverlayBackend::destroy(ExternalSurfaceId id) {
  auto found = impl_->surfaces.find(id);
  if (found != impl_->surfaces.end() && found->second.controller != nullptr) {
    found->second.controller->Close();
  }
  impl_->surfaces.erase(id);
}

bool WebView2OverlayBackend::focus(ExternalSurfaceId id, std::string* error) {
  if (error == nullptr) return false;
  auto found = impl_->surfaces.find(id);
  if (found == impl_->surfaces.end() || found->second.controller == nullptr) {
    *error = "WebView2 surface is not focusable";
    return false;
  }
  const HRESULT hr = found->second.controller->MoveFocus(
      COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
  if (FAILED(hr)) {
    *error = HResultMessage(hr, "WebView2 MoveFocus");
    return false;
  }
  return true;
}

void WebView2OverlayBackend::focusCanvas() {
  if (impl_->options.owner_window != nullptr) {
    SetFocus(impl_->options.owner_window);
  }
}

}  // namespace canvas::poc05::windows
