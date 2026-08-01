#include "whiteboard_app.h"

#include "canvas/document/embedded_transform.h"
#include "canvas/storage/document_codec.h"
#include "platform/windows/document_store.h"
#include "platform/windows/named_pipe_server.h"
#include "platform/windows/webview2_media_source.h"
#include "platform/windows/webview2_video_restore.h"
#include "platform/windows/win_pointer_adapter.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace canvas::windows {

namespace {

constexpr wchar_t kWindowClassName[] = L"MostormCanvasWindow";
constexpr WORD kSystemArrowCursorId = 32512U;
constexpr UINT kIpcMessage = WM_APP + 0x4D;
constexpr UINT kEmbeddedCompletionMessage = WM_APP + 0x4E;
constexpr UINT_PTR kEmbeddedCompletionTimeoutTimer = 0xCA22U;
constexpr UINT kEmbeddedCompletionTimeoutMs = 30'000U;
constexpr std::size_t kMaximumQueuedIpcMessages = 128U;
constexpr std::size_t kMaximumQueuedIpcBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumEmbeddedSourceBytes = 1024U * 1024U;
constexpr float kMaximumEmbeddedCoordinate = 1'000'000.0F;

int hresultExitCode(HRESULT hr) {
  return FAILED(hr) ? static_cast<int>(hr) : 1;
}

bool isValidEmbeddedBounds(const core::Rect& bounds) {
  if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
      !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
      bounds.width <= 0.0F || bounds.height <= 0.0F ||
      bounds.width > kMaximumEmbeddedCoordinate ||
      bounds.height > kMaximumEmbeddedCoordinate ||
      std::fabs(bounds.x) > kMaximumEmbeddedCoordinate ||
      std::fabs(bounds.y) > kMaximumEmbeddedCoordinate) {
    return false;
  }
  const double right = static_cast<double>(bounds.x) + bounds.width;
  const double bottom = static_cast<double>(bounds.y) + bounds.height;
  return std::isfinite(right) && std::isfinite(bottom) &&
         std::fabs(right) <= kMaximumEmbeddedCoordinate &&
         std::fabs(bottom) <= kMaximumEmbeddedCoordinate;
}

std::optional<std::size_t> queuedIpcMessageBytes(const ipc::Message& message) {
  try {
    const std::size_t payloadBytes = message.payload.dump().size();
    constexpr std::size_t kEnvelopeBytes = 96U;
    if (message.type.size() > (std::numeric_limits<std::size_t>::max)() -
                                  message.requestId.size() ||
        payloadBytes > (std::numeric_limits<std::size_t>::max)() -
                           message.type.size() - message.requestId.size() -
                           kEnvelopeBytes) {
      return std::nullopt;
    }
    return kEnvelopeBytes + message.type.size() + message.requestId.size() +
           payloadBytes;
  } catch (...) {
    return std::nullopt;
  }
}

input::PointerPhase pointerPhaseForMessage(UINT message) {
  switch (message) {
    case WM_POINTERDOWN:
      return input::PointerPhase::Down;
    case WM_POINTERUP:
      return input::PointerPhase::Up;
    case WM_POINTERCAPTURECHANGED:
      return input::PointerPhase::Cancel;
    case WM_POINTERUPDATE:
    default:
      return input::PointerPhase::Move;
  }
}

std::optional<EmbeddedMouseButton> embeddedMouseButtonForMessage(
    UINT message, WPARAM wParam) {
  switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
      return EmbeddedMouseButton::Left;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
      return EmbeddedMouseButton::Right;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
      return EmbeddedMouseButton::Middle;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
      return GET_XBUTTON_WPARAM(wParam) == XBUTTON2
                 ? EmbeddedMouseButton::X2
                 : EmbeddedMouseButton::X1;
    default:
      return std::nullopt;
  }
}

bool isEmbeddedMouseButtonDown(UINT message) {
  return message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
         message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK ||
         message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK ||
         message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK;
}

bool isEmbeddedMouseButtonUp(UINT message) {
  return message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
         message == WM_MBUTTONUP || message == WM_XBUTTONUP;
}

std::optional<std::wstring> utf8ToWide(std::string_view value) {
  if (value.empty()) return std::wstring{};
  if (value.size() >
      static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return std::nullopt;
  }
  const int sourceLength = static_cast<int>(value.size());
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(), sourceLength, nullptr,
                                         0);
  if (length <= 0) return std::nullopt;
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          sourceLength, result.data(), length) != length) {
    return std::nullopt;
  }
  return result;
}

std::optional<std::string> wideToUtf8(std::wstring_view value) {
  if (value.empty()) return std::string{};
  if (value.size() >
      static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return std::nullopt;
  }
  const int sourceLength = static_cast<int>(value.size());
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                         value.data(), sourceLength, nullptr,
                                         0, nullptr, nullptr);
  if (length <= 0) return std::nullopt;
  std::string result(static_cast<std::size_t>(length), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          sourceLength, result.data(), length, nullptr,
                          nullptr) != length) {
    return std::nullopt;
  }
  return result;
}

std::optional<std::wstring> percentEncodeQueryComponent(
    std::wstring_view value) {
  const auto utf8 = wideToUtf8(value);
  if (!utf8) return std::nullopt;
  constexpr char kHex[] = "0123456789ABCDEF";
  std::wstring result;
  result.reserve(utf8->size() * 3U);
  for (const unsigned char byte : *utf8) {
    const bool unreserved =
        (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' ||
        byte == '_' || byte == '~';
    if (unreserved) {
      result.push_back(static_cast<wchar_t>(byte));
    } else {
      result.push_back(L'%');
      result.push_back(static_cast<wchar_t>(kHex[byte >> 4U]));
      result.push_back(static_cast<wchar_t>(kHex[byte & 0x0FU]));
    }
  }
  return result;
}

}  // namespace

struct WhiteboardApp::EmbeddedLoadCallbackState {
  WhiteboardApp* owner = nullptr;
  bool active = true;
};

WhiteboardApp::~WhiteboardApp() {
  closing_ = true;
  cancelPendingOpen();
  stopIpc();
}

int WhiteboardApp::run(HINSTANCE instance, int commandShow,
                       const WhiteboardRunOptions& options) {
  lastError_ = S_OK;
  closing_ = false;
  videoPath_ = options.videoPath;
  ipcQueueOverflowed_ = false;
  if (instance == nullptr) {
    return hresultExitCode(E_INVALIDARG);
  }
  // Decode before creating any native surfaces. The codec returns a complete
  // temporary Document, so a malformed file can never partially replace the
  // in-memory document used by the renderer.
  if (options.openPath) {
    const HRESULT openResult = openDocument(*options.openPath);
    if (FAILED(openResult)) return hresultExitCode(openResult);
  }

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.hInstance = instance;
  windowClass.lpfnWndProc = &WhiteboardApp::windowProc;
  windowClass.lpszClassName = kWindowClassName;
  windowClass.hCursor =
      LoadCursorW(nullptr, MAKEINTRESOURCEW(kSystemArrowCursorId));
  windowClass.style = CS_HREDRAW | CS_VREDRAW;

  if (RegisterClassExW(&windowClass) == 0) {
    return hresultExitCode(HRESULT_FROM_WIN32(GetLastError()));
  }

  const HWND window = CreateWindowExW(
      WS_EX_APPWINDOW, kWindowClassName, L"Mostorm Canvas", WS_POPUP,
      CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, instance,
      this);
  if (window == nullptr) {
    const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(hr);
  }
  window_ = window;

  const HRESULT compositionResult = composition_.initialize(window);
  if (FAILED(compositionResult)) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(compositionResult);
  }

  const HRESULT gpuResult = gpu_.initialize();
  if (FAILED(gpuResult)) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(gpuResult);
  }
  constexpr int kCanvasWidth = 1280;
  constexpr int kCanvasHeight = 720;
  const bool openedDocumentHasEmbedded = std::any_of(
      document_.nodes().begin(), document_.nodes().end(),
      [](const document::Node& node) {
        return node.layer == document::LayerClass::Embedded &&
               std::holds_alternative<document::EmbeddedNode>(node.payload);
      });
  const bool ipcEnabled = options.ipcPipe.has_value() &&
                          options.sessionToken.has_value();
  const bool diagnosticLayers = ipcEnabled || options.selfTestLayers ||
                                options.selfTestEmbedded ||
                                options.selfTestDocument ||
                                openedDocumentHasEmbedded;
  inputRouter_.setFingerDrawEnabled(true);
  inputRouter_.setMode(diagnosticLayers ? input::InputMode::Interact
                                        : input::InputMode::Draw);
  HRESULT layerResult = baseLayer_.initialize(
      gpu_, composition_, VisualSlot::BaseCanvas, kCanvasWidth, kCanvasHeight,
      false);
  if (SUCCEEDED(layerResult)) {
    layerResult = annotationLayer_.initialize(
        gpu_, composition_, VisualSlot::Annotation, kCanvasWidth,
        kCanvasHeight, true);
  }
  if (SUCCEEDED(layerResult) && diagnosticLayers) {
    layerResult = embeddedLayer_.initialize(
        gpu_, composition_, VisualSlot::EmbeddedContent, kCanvasWidth,
        kCanvasHeight, true);
  }
  if (SUCCEEDED(layerResult) && diagnosticLayers) {
    layerResult = chromeLayer_.initialize(
        gpu_, composition_, VisualSlot::InteractionChrome, kCanvasWidth,
        kCanvasHeight, true);
  }
  if (FAILED(layerResult)) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(layerResult);
  }
  if (diagnosticLayers) {
    baseLayer_.setClearColorArgb(0xFF00AA00U);
    const bool populated = ipcEnabled
                               ? true
                               : (options.openPath
                               ? true
                               : (options.selfTestEmbedded ||
                                          options.selfTestDocument
                                      ? populateEmbeddedSelfTestDocument()
                                      : populateSelfTestDocument()));
    if (!populated) {
      DestroyWindow(window);
      UnregisterClassW(kWindowClassName, instance);
      return hresultExitCode(E_FAIL);
    }

    const auto addWebView =
        [this, window](const document::NodeId& nodeId,
                       WebView2Surface::Options surfaceOptions,
                       std::wstring_view uri,
                       const std::optional<std::wstring>& initialMessage =
                           std::nullopt) -> HRESULT {
      const document::Node* node = document_.find(nodeId);
      if (node == nullptr) return E_INVALIDARG;
      auto surface = std::make_unique<WebView2Surface>(
          composition_, window, std::move(surfaceOptions));
      HRESULT result = setSurfaceBounds(*surface, node->bounds);
      if (FAILED(result)) return result;
      surface->setInteractive(false);
      result = setSurfaceVisible(*surface, true);
      if (FAILED(result)) return result;
      result = surface->initialize();
      if (SUCCEEDED(result)) result = surface->navigate(uri);
      if (SUCCEEDED(result) && initialMessage) {
        result = surface->postMessage(*initialMessage);
      }
      if (FAILED(result)) return result;
      embeddedWebViews_.push_back(
          HostedWebView{nodeId, std::move(surface)});
      return S_OK;
    };

    if (ipcEnabled) {
      if (options.openPath) {
        layerResult =
            restoreEmbeddedSurfaces(document_, embeddedWebViews_, true);
      }
      // Without --open, Electron commands add embedded nodes on the UI thread.
      // Do not create diagnostic content or a WebView before a command arrives.
    } else if (options.selfTestEmbedded && !options.openPath) {
      WebView2Surface::Options contentOptions;
      contentOptions.canvasLocalFolder = L"web";
      layerResult = addWebView(
          "rich-text-1", contentOptions,
          L"https://canvas.local/richtext.html?nodeId=rich-text-1");

      detail::LocalMediaSource mediaSource;
      if (SUCCEEDED(layerResult) && options.videoPath) {
        layerResult =
            detail::approveLocalMediaFile(*options.videoPath, mediaSource);
      }
      WebView2Surface::Options videoOptions = contentOptions;
      std::optional<std::wstring> videoMessage;
      if (SUCCEEDED(layerResult) && options.videoPath) {
        videoOptions.mediaCanvasLocalFolder = mediaSource.folder;
        videoMessage =
            detail::buildSetVideoSourceMessage(L"video-1", mediaSource.uri);
        if (!videoMessage) layerResult = E_INVALIDARG;
      }
      if (SUCCEEDED(layerResult)) {
        layerResult = addWebView(
            "video-1", std::move(videoOptions),
            L"https://canvas.local/video.html?nodeId=video-1", videoMessage);
      }
      if (SUCCEEDED(layerResult)) {
        layerResult = addWebView("web-1", std::move(contentOptions),
                                 L"https://example.com/");
      }
    } else if (options.openPath || options.selfTestDocument) {
      // Recreate persisted embedded surfaces from their versioned node data.
      // Only packaged canvas.local assets and HTTPS pages are accepted by the
      // WebView2 navigation policy. A video path is approved through the same
      // handle-based helper used by the standalone embedded diagnostic.
      for (const auto& node : document_.nodes()) {
        const auto* embedded =
            std::get_if<document::EmbeddedNode>(&node.payload);
        if (embedded == nullptr ||
            node.layer != document::LayerClass::Embedded) {
          continue;
        }
        if (node.id.empty() || node.id.size() > 1024U ||
            embedded->source.empty() ||
            embedded->source.size() > 1024U * 1024U) {
          layerResult = E_INVALIDARG;
          break;
        }
        const auto source = utf8ToWide(embedded->source);
        const auto wideNodeId = utf8ToWide(node.id);
        if (!source || !wideNodeId || source->empty() || wideNodeId->empty() ||
            wideNodeId->size() > 256U) {
          layerResult = E_INVALIDARG;
          break;
        }
        WebView2Surface::Options surfaceOptions;
        std::wstring uri = *source;
        std::optional<std::wstring> initialMessage;
        if (embedded->kind == document::EmbeddedKind::Video) {
          // A persisted video node always restores the packaged adapter. Its
          // media URL is delivered through the versioned host bridge so
          // play/pause/seek and telemetry keep working after reopening.
          const auto plan =
              detail::buildVideoRestorePlan(*wideNodeId, *source);
          if (!plan) {
            layerResult = E_INVALIDARG;
            break;
          }
          surfaceOptions.canvasLocalFolder = L"web";
          uri = plan->navigationUri;
          if (options.videoPath) {
            detail::LocalMediaSource mediaSource;
            layerResult =
                detail::approveLocalMediaFile(*options.videoPath, mediaSource);
            if (FAILED(layerResult)) break;
            surfaceOptions.mediaCanvasLocalFolder = mediaSource.folder;
            initialMessage = detail::buildSetVideoSourceMessage(
                *wideNodeId, mediaSource.uri);
            if (!initialMessage) {
              layerResult = E_INVALIDARG;
              break;
            }
          } else {
            switch (plan->mediaAction) {
              case detail::VideoRestoreMediaAction::None:
                break;
              case detail::VideoRestoreMediaAction::UsePersistedRemote:
                if (!plan->initialMessage) {
                  layerResult = E_INVALIDARG;
                  break;
                }
                initialMessage = plan->initialMessage;
                break;
              case detail::VideoRestoreMediaAction::ApprovePersistedLocalFile: {
                detail::LocalMediaSource mediaSource;
                layerResult =
                    detail::approveLocalMediaFile(*source, mediaSource);
                if (FAILED(layerResult)) break;
                surfaceOptions.mediaCanvasLocalFolder = mediaSource.folder;
                initialMessage = detail::buildSetVideoSourceMessage(
                    *wideNodeId, mediaSource.uri);
                if (!initialMessage) layerResult = E_INVALIDARG;
                break;
              }
              case detail::VideoRestoreMediaAction::Reject:
                // A media.canvas.local URL alone is not durable: its approved
                // folder mapping is process-local. Persist the original local
                // path instead, or provide --video when reopening. Invalid
                // remote sources are rejected before a page message is sent.
                layerResult = E_ACCESSDENIED;
                break;
            }
            if (FAILED(layerResult)) break;
          }
        } else {
          const auto encodedNodeId = percentEncodeQueryComponent(*wideNodeId);
          if (!encodedNodeId) {
            layerResult = E_INVALIDARG;
            break;
          }
          const bool packagedSource =
              _wcsnicmp(uri.c_str(), L"https://canvas.local/", 21) == 0;
          if (packagedSource &&
              embedded->kind == document::EmbeddedKind::RichText) {
            surfaceOptions.canvasLocalFolder = L"web";
            uri = L"https://canvas.local/richtext.html?nodeId=" +
                  *encodedNodeId;
          } else if (packagedSource) {
            surfaceOptions.canvasLocalFolder = L"web";
          }
        }
        if (SUCCEEDED(layerResult)) {
          layerResult = addWebView(node.id, std::move(surfaceOptions), uri,
                                   initialMessage);
        }
        if (FAILED(layerResult)) break;
      }
    } else {
      WebView2Surface::Options diagnosticOptions;
      diagnosticOptions.allowTestDataUrls = true;
      constexpr auto kSelfTestPage =
          L"data:text/html,%3C!doctype%20html%3E%3Cmeta%20charset=utf-8%3E"
          L"%3Cstyle%3Ehtml,body%7Bmargin:0;width:100%25;height:100%25;"
          L"display:grid;place-items:center;background:%23ddd;font:24px%20"
          L"sans-serif%7D%3C/style%3EEmbedded%20WebView2";
      layerResult = addWebView("self-test-embedded",
                               std::move(diagnosticOptions), kSelfTestPage);
    }
    if (FAILED(layerResult)) {
      DestroyWindow(window);
      UnregisterClassW(kWindowClassName, instance);
      return hresultExitCode(layerResult);
    }
  }
  HRESULT renderResult =
      baseLayer_.render(document_, document::LayerClass::Base);
  if (SUCCEEDED(renderResult) && diagnosticLayers) {
    renderResult = embeddedLayer_.render(
        document_, document::LayerClass::Embedded);
  }
  if (SUCCEEDED(renderResult)) {
    renderResult = annotationLayer_.render(
        document_, document::LayerClass::Annotation);
  }
  if (SUCCEEDED(renderResult) && diagnosticLayers) {
    renderResult = chromeLayer_.render(document_, document::LayerClass::Chrome);
  }
  if (FAILED(renderResult)) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(renderResult);
  }

  if (ipcEnabled) {
    ipcServer_ = std::make_unique<NamedPipeServer>();
    std::string pipeError;
    const bool started = ipcServer_->start(
        *options.ipcPipe, *options.sessionToken,
        [this, window](const ipc::Message& message,
                       NamedPipeServer::ConnectionId connectionId) {
          const auto bytes = queuedIpcMessageBytes(message);
          bool postIpc = false;
          bool closeForOverflow = false;
          {
            std::lock_guard<std::mutex> lock(ipcMessagesMutex_);
            if (!bytes || *bytes > kMaximumQueuedIpcBytes ||
                ipcMessages_.size() >= kMaximumQueuedIpcMessages ||
                *bytes > kMaximumQueuedIpcBytes - ipcQueuedBytes_) {
              closeForOverflow = !ipcQueueOverflowed_;
              ipcQueueOverflowed_ = true;
            } else {
              ipcMessages_.push_back(QueuedIpcMessage{message, connectionId});
              ipcQueuedBytes_ += *bytes;
              if (!ipcMessagePosted_) {
                ipcMessagePosted_ = true;
                postIpc = true;
              }
            }
          }
          if (closeForOverflow) {
            OutputDebugStringA("Canvas IPC queue overflow; closing window\n");
            (void)PostMessageW(window, WM_CLOSE, 0, 0);
            return;
          }
          // The pipe thread never touches Document, D3D, DComp, or WebView.
          // The UI thread drains this queue in windowProc below.
          if (postIpc) (void)PostMessageW(window, kIpcMessage, 0, 0);
        },
        pipeError);
    if (!started) {
      DestroyWindow(window);
      UnregisterClassW(kWindowClassName, instance);
      return hresultExitCode(E_FAIL);
    }
  }

  // Saving is an explicit command-line operation, intentionally performed
  // outside all pointer callbacks. Copying first gives the store an immutable
  // snapshot even if a future caller invokes save from another UI action.
  if (options.savePath) {
    const HRESULT saveResult = saveDocument(*options.savePath);
    if (FAILED(saveResult)) {
      DestroyWindow(window);
      UnregisterClassW(kWindowClassName, instance);
      return hresultExitCode(saveResult);
    }
  }

  ShowWindow(window, commandShow);
  UpdateWindow(window);
  if (options.selfTestDocument) {
    // The document round-trip diagnostic is intended for automation and must
    // not leave a hidden message loop running after the requested save.
    PostMessageW(window, WM_CLOSE, 0, 0);
  }

  MSG message{};
  int messageResult = 0;
  while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  const auto messageExit = canvas::app::finishWhiteboardMessageLoop(
      messageResult, static_cast<std::intptr_t>(message.wParam), *this);

  UnregisterClassW(kWindowClassName, instance);
  if (FAILED(lastError_)) return hresultExitCode(lastError_);
  return messageExit.failed
             ? hresultExitCode(HRESULT_FROM_WIN32(messageExit.errorCode))
             : static_cast<int>(messageExit.quitCode);
}

LRESULT CALLBACK WhiteboardApp::windowProc(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }

  if (message == WM_DESTROY) {
    auto* app = reinterpret_cast<WhiteboardApp*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app != nullptr) {
      app->closing_ = true;
      app->cancelPendingOpen();
      app->stopIpc();
      app->window_ = nullptr;
      (void)app->forwardMouseToEmbedded(window, WM_CANCELMODE, 0, 0);
      app->embeddedWebViews_.clear();
    }
    PostQuitMessage(0);
    return 0;
  }

  if (message == kIpcMessage) {
    auto* app = reinterpret_cast<WhiteboardApp*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app != nullptr) app->handleIpcMessages();
    return 0;
  }

  if (message == kEmbeddedCompletionMessage) {
    auto* app = reinterpret_cast<WhiteboardApp*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app != nullptr) app->handleEmbeddedLoadCompletions();
    return 0;
  }

  if (message == WM_TIMER) {
    auto* app = reinterpret_cast<WhiteboardApp*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app != nullptr && wParam == app->embeddedCompletionTimeoutTimerId_ &&
        app->pendingOpen_ != nullptr) {
      app->failPendingOpen(HRESULT_FROM_WIN32(ERROR_TIMEOUT),
                           "embedded document load timed out");
      return 0;
    }
  }

  if (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE ||
      message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
      message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN ||
      message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK ||
      message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
      message == WM_MBUTTONDBLCLK || message == WM_XBUTTONDOWN ||
      message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK ||
      message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ||
      message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
    auto* app = reinterpret_cast<WhiteboardApp*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app != nullptr) {
      if (app->pendingOpen_ != nullptr) return 0;
      const HRESULT forwardResult =
          app->forwardMouseToEmbedded(window, message, wParam, lParam);
      if (forwardResult == S_OK) {
        if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ||
            message == WM_XBUTTONDBLCLK) {
          return TRUE;
        }
        return 0;
      }
      if (FAILED(forwardResult) && forwardResult != E_PENDING) {
        app->lastError_ = forwardResult;
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
    }
  }

  if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE ||
      message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
    auto* app = reinterpret_cast<WhiteboardApp*>(GetWindowLongPtrW(
        window, GWLP_USERDATA));
    if (app == nullptr) {
      return DefWindowProcW(window, message, wParam, lParam);
    }
    if (app->pendingOpen_ != nullptr) {
      if (message == WM_POINTERUP ||
          message == WM_POINTERCAPTURECHANGED) {
        ReleaseCapture();
      }
      return 0;
    }

    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (message == WM_POINTERCAPTURECHANGED) {
      const HRESULT strokeCancel = app->cancelActivePointer(pointerId);
      const HRESULT embeddedCancel = app->cancelEmbeddedTouch(pointerId);
      const HRESULT cancelResult =
          FAILED(strokeCancel) ? strokeCancel : embeddedCancel;
      if (FAILED(cancelResult) && cancelResult != E_PENDING) {
        app->lastError_ = cancelResult;
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      ReleaseCapture();
      return 0;
    }

    POINTER_INPUT_TYPE pointerType{};
    if (!GetPointerType(pointerId, &pointerType)) {
      const HRESULT strokeCancel = app->cancelActivePointer(pointerId);
      const HRESULT embeddedCancel = app->cancelEmbeddedTouch(pointerId);
      const HRESULT cancelResult =
          FAILED(strokeCancel) ? strokeCancel : embeddedCancel;
      if (FAILED(cancelResult) && cancelResult != E_PENDING) {
        app->lastError_ = cancelResult;
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      if (message == WM_POINTERUP) {
        ReleaseCapture();
      }
      return DefWindowProcW(window, message, wParam, lParam);
    }

    const input::PointerPhase phase = pointerPhaseForMessage(message);
    std::vector<input::PointerSample> samples;
    if (pointerType == PT_PEN) {
      samples = WinPointerAdapter::readPenHistory(window, pointerId, phase);
    } else if (pointerType == PT_TOUCH) {
      samples = WinPointerAdapter::readTouchHistory(window, pointerId, phase);
    } else {
      const HRESULT strokeCancel = app->cancelActivePointer(pointerId);
      const HRESULT embeddedCancel = app->cancelEmbeddedTouch(pointerId);
      const HRESULT cancelResult =
          FAILED(strokeCancel) ? strokeCancel : embeddedCancel;
      if (FAILED(cancelResult) && cancelResult != E_PENDING) {
        app->lastError_ = cancelResult;
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      if (message == WM_POINTERUP) {
        ReleaseCapture();
      }
      return DefWindowProcW(window, message, wParam, lParam);
    }

    if (samples.empty()) {
      const HRESULT strokeCancel = app->cancelActivePointer(pointerId);
      const HRESULT embeddedCancel = app->cancelEmbeddedTouch(pointerId);
      const HRESULT cancelResult =
          FAILED(strokeCancel) ? strokeCancel : embeddedCancel;
      if (FAILED(cancelResult) && cancelResult != E_PENDING) {
        app->lastError_ = cancelResult;
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      if (message == WM_POINTERUP) {
        ReleaseCapture();
      }
      return 0;
    }

    // Win32 history is oldest-first. Preserve the DOWN edge on the oldest
    // record and the UP edge on the newest; all intervening records move.
    for (std::size_t index = 0; index < samples.size(); ++index) {
      if (phase == input::PointerPhase::Down && index != 0) {
        samples[index].phase = input::PointerPhase::Move;
      } else if (phase == input::PointerPhase::Up &&
                 index + 1 < samples.size()) {
        samples[index].phase = input::PointerPhase::Move;
      }
    }
    if (pointerType == PT_TOUCH) {
      const HRESULT forwardResult = app->forwardTouchToEmbedded(
          message, pointerId, samples.back());
      if (forwardResult != S_FALSE) {
        if (FAILED(forwardResult) && forwardResult != E_PENDING) {
          app->lastError_ = forwardResult;
          PostMessageW(window, WM_CLOSE, 0, 0);
        }
        if (message == WM_POINTERDOWN && forwardResult == S_OK) {
          SetCapture(window);
        } else if (message == WM_POINTERUP || FAILED(forwardResult)) {
          ReleaseCapture();
        }
        return 0;
      }
    }
    const HRESULT sampleResult = app->onPointerSamples(std::move(samples));
    if (FAILED(sampleResult)) {
      app->lastError_ = sampleResult;
      PostMessageW(window, WM_CLOSE, 0, 0);
      if (message == WM_POINTERUP ||
          message == WM_POINTERCAPTURECHANGED) {
        ReleaseCapture();
      }
      return 0;
    }

    if (message == WM_POINTERDOWN) {
      SetCapture(window);
    } else if (message == WM_POINTERUP ||
               message == WM_POINTERCAPTURECHANGED) {
      ReleaseCapture();
    }
    return 0;
  }

  return DefWindowProcW(window, message, wParam, lParam);
}

HRESULT WhiteboardApp::forwardMouseToEmbedded(HWND window, UINT message,
                                               WPARAM wParam, LPARAM lParam) {
  const auto mergeResult = [](HRESULT& firstResult, HRESULT result) {
    if (SUCCEEDED(firstResult) && FAILED(result)) firstResult = result;
  };

  const auto applyCancellation = [this, window, &mergeResult](
                                     const EmbeddedMouseDecision& decision) {
        if (!decision.handled()) return S_FALSE;
        HRESULT firstResult = S_OK;
        WebView2Surface* surface = embeddedWebView(embeddedMouseNodeId_);
        if (surface != nullptr) {
          surface->setInteractive(true);
          if (decision.cancelButtons != 0) {
            mergeResult(firstResult,
                        surface->cancelMouseButtons(decision.cancelButtons));
          } else if (decision.sendLeave) {
            mergeResult(firstResult,
                        surface->forwardMouseMessage(WM_MOUSELEAVE, 0, 0));
          }
          surface->setInteractive(embeddedMouseSession_.hovered() ||
                                  embeddedMouseSession_.buttons() != 0);
        } else {
          mergeResult(firstResult, E_UNEXPECTED);
        }
        if (decision.releaseCapture && GetCapture() == window) {
          ReleaseCapture();
        }
        return firstResult;
      };

  if (message == WM_CAPTURECHANGED) {
    // Capture already belongs elsewhere. Do not call ReleaseCapture; balance
    // each WebView button with LEAVE + a surface-local outside synthetic UP.
    const HRESULT result =
        applyCancellation(embeddedMouseSession_.captureLost());
    if (embeddedMouseSession_.buttons() == 0 &&
        !embeddedMouseSession_.hovered()) {
      embeddedMouseNodeId_.reset();
    }
    return result;
  }
  if (message == WM_CANCELMODE) {
    const HRESULT result = applyCancellation(embeddedMouseSession_.disable());
    embeddedMouseNodeId_.reset();
    return result;
  }
  if (embeddedWebViews_.empty()) {
    (void)embeddedMouseSession_.disable();
    embeddedMouseNodeId_.reset();
    return S_FALSE;
  }

  POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
  if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
    if (!ScreenToClient(window, &point)) return S_FALSE;
  }
  const auto hit = hitEmbedded(
      core::Vec2{static_cast<float>(point.x), static_cast<float>(point.y)});
  const bool buttonDown = isEmbeddedMouseButtonDown(message);
  const bool buttonUp = isEmbeddedMouseButtonUp(message);
  const auto button = embeddedMouseButtonForMessage(message, wParam);

  if (message == WM_MOUSELEAVE) {
    const HRESULT result =
        applyCancellation(embeddedMouseSession_.nativeLeave());
    if (embeddedMouseSession_.buttons() == 0) embeddedMouseNodeId_.reset();
    return result;
  }

  // One native mouse session belongs to one WebView. When an uncaptured hover
  // crosses into another child, balance the previous surface before routing
  // the same event to the new hit target.
  if (embeddedMouseSession_.buttons() == 0 && embeddedMouseNodeId_ &&
      hit != embeddedMouseNodeId_) {
    const HRESULT leaveResult =
        applyCancellation(embeddedMouseSession_.nativeLeave());
    if (FAILED(leaveResult)) return leaveResult;
    embeddedMouseNodeId_.reset();
  }
  if (buttonDown && embeddedMouseSession_.buttons() == 0) {
    inputRouter_.setActiveEmbeddedNode(hit);
  }
  const auto route = inputRouter_.route(input::PointerKind::Mouse, hit);
  const bool routedToEmbedded =
      route.target == input::InputTarget::EmbeddedSurface &&
      embeddedWebView(hit) != nullptr;
  if (embeddedMouseSession_.buttons() == 0 && routedToEmbedded) {
    embeddedMouseNodeId_ = hit;
  }
  WebView2Surface* surface = embeddedWebView(embeddedMouseNodeId_);

  EmbeddedMouseDecision decision;
  if (buttonDown && button) {
    decision = embeddedMouseSession_.buttonDown(*button, routedToEmbedded);
  } else if (buttonUp && button) {
    decision = embeddedMouseSession_.buttonUp(*button, routedToEmbedded);
  } else {
    // MOVE and wheel both establish/leave hover. While captured, an outside
    // MOVE remains forwardable so WebView receives the drag position.
    decision = embeddedMouseSession_.move(routedToEmbedded);
  }
  if (!decision.handled()) {
    if (surface != nullptr) surface->setInteractive(false);
    if (embeddedMouseSession_.buttons() == 0 &&
        !embeddedMouseSession_.hovered()) {
      embeddedMouseNodeId_.reset();
    }
    return S_FALSE;
  }
  if (surface == nullptr) {
    (void)embeddedMouseSession_.disable();
    embeddedMouseNodeId_.reset();
    return E_UNEXPECTED;
  }

  HRESULT firstResult = S_OK;
  const auto recordForward = [](HRESULT result) {
    if (result == S_OK) return S_OK;
    if (FAILED(result)) return result;
    // S_FALSE means the surface gate declined the event; treating it as a
    // successful DOWN would leave a host button bit with no WebView DOWN.
    return E_UNEXPECTED;
  };
  if (decision.startTrackingLeave) {
    TRACKMOUSEEVENT tracking{};
    tracking.cbSize = sizeof(tracking);
    tracking.dwFlags = TME_LEAVE;
    tracking.hwndTrack = window;
    SetLastError(ERROR_SUCCESS);
    if (!TrackMouseEvent(&tracking)) {
      const DWORD error = GetLastError();
      mergeResult(firstResult,
                  error == ERROR_SUCCESS ? E_FAIL
                                         : HRESULT_FROM_WIN32(error));
    }
  }

  surface->setInteractive(true);
  if (buttonDown && decision.forward) {
    mergeResult(firstResult, recordForward(surface->focus()));
  }
  if (decision.sendLeave) {
    mergeResult(firstResult, recordForward(
                                 surface->forwardMouseMessage(
                                     WM_MOUSELEAVE, 0, 0)));
  }
  if (decision.forward) {
    mergeResult(firstResult, recordForward(
                                 surface->forwardMouseMessage(
                                     message, wParam, lParam)));
  }

  if (decision.capture && SUCCEEDED(firstResult)) {
    SetCapture(window);
    if (GetCapture() != window) mergeResult(firstResult, E_FAIL);
  }
  if (decision.releaseCapture && GetCapture() == window) {
    ReleaseCapture();
  }

  if (FAILED(firstResult)) {
    const EmbeddedMouseDecision cleanup = embeddedMouseSession_.disable();
    const HRESULT cleanupResult = applyCancellation(cleanup);
    mergeResult(firstResult, cleanupResult);
  } else {
    surface->setInteractive(embeddedMouseSession_.hovered() ||
                            embeddedMouseSession_.buttons() != 0);
    if (embeddedMouseSession_.buttons() == 0 &&
        !embeddedMouseSession_.hovered()) {
      embeddedMouseNodeId_.reset();
    }
  }
  return firstResult;
}

HRESULT WhiteboardApp::forwardTouchToEmbedded(
    UINT message, UINT32 pointerId, const input::PointerSample& sample) {
  if (embeddedWebViews_.empty()) return S_FALSE;
  if (activeEmbeddedPointerId_ && *activeEmbeddedPointerId_ != pointerId) {
    return S_FALSE;
  }

  if (!activeEmbeddedPointerId_) {
    if (message != WM_POINTERDOWN) return S_FALSE;
    const auto hit = hitEmbedded(sample.screenPosition);
    inputRouter_.setActiveEmbeddedNode(hit);
    const auto route = inputRouter_.route(input::PointerKind::Touch, hit);
    WebView2Surface* hitSurface = embeddedWebView(hit);
    if (route.target != input::InputTarget::EmbeddedSurface ||
        hitSurface == nullptr) {
      if (hitSurface != nullptr) hitSurface->setInteractive(false);
      return S_FALSE;
    }
    activeEmbeddedPointerId_ = pointerId;
    activeEmbeddedTouchNodeId_ = hit;
    hitSurface->setInteractive(true);
    const HRESULT focusResult = hitSurface->focus();
    if (FAILED(focusResult)) {
      activeEmbeddedPointerId_.reset();
      activeEmbeddedTouchNodeId_.reset();
      hitSurface->setInteractive(false);
      return focusResult;
    }
  }

  WebView2Surface* surface = embeddedWebView(activeEmbeddedTouchNodeId_);
  if (surface == nullptr) {
    activeEmbeddedPointerId_.reset();
    activeEmbeddedTouchNodeId_.reset();
    return E_UNEXPECTED;
  }
  const HRESULT result = surface->forwardTouchMessage(message, pointerId);
  if (result != S_OK && message != WM_POINTERCAPTURECHANGED) {
    // Best effort: preserve the WebView pointer lifecycle before releasing
    // the native session. The cleanup below always runs even if this fails.
    surface->forwardTouchMessage(WM_POINTERCAPTURECHANGED, pointerId);
  }
  if (message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED ||
      result != S_OK) {
    activeEmbeddedPointerId_.reset();
    activeEmbeddedTouchNodeId_.reset();
    surface->setInteractive(false);
  }
  return result;
}

HRESULT WhiteboardApp::cancelEmbeddedTouch(UINT32 pointerId) {
  if (!activeEmbeddedPointerId_ ||
      *activeEmbeddedPointerId_ != pointerId) {
    return S_FALSE;
  }

  HRESULT result = S_OK;
  WebView2Surface* surface = embeddedWebView(activeEmbeddedTouchNodeId_);
  if (surface != nullptr) {
    surface->setInteractive(true);
    result = surface->forwardTouchMessage(
        WM_POINTERCAPTURECHANGED, pointerId);
    surface->setInteractive(false);
  }
  activeEmbeddedPointerId_.reset();
  activeEmbeddedTouchNodeId_.reset();
  return result;
}

std::optional<document::NodeId> WhiteboardApp::hitEmbedded(
    core::Vec2 point) const {
  for (auto it = document_.nodes().rbegin(); it != document_.nodes().rend();
       ++it) {
    if (it->layer == document::LayerClass::Embedded &&
        std::holds_alternative<document::EmbeddedNode>(it->payload) &&
        it->bounds.contains(point)) {
      return it->id;
    }
  }
  return std::nullopt;
}

WebView2Surface* WhiteboardApp::embeddedWebView(
    const std::optional<document::NodeId>& nodeId) const {
  if (!nodeId) return nullptr;
  for (const auto& hosted : embeddedWebViews_) {
    if (hosted.nodeId == *nodeId) return hosted.surface.get();
  }
  return nullptr;
}

document::LayerClass WhiteboardApp::activeDocumentLayer() const {
  return activeRoute_.target == input::InputTarget::Annotation
             ? document::LayerClass::Annotation
             : document::LayerClass::Base;
}

SkiaSwapChainLayer& WhiteboardApp::activeSwapChainLayer() {
  return activeRoute_.target == input::InputTarget::Annotation
             ? annotationLayer_
             : baseLayer_;
}

HRESULT WhiteboardApp::cancelActivePointer(std::uint64_t pointerId) {
  if (!activeStroke_) return S_OK;
  if (!activePointerId_ || *activePointerId_ != pointerId) return S_FALSE;
  input::PointerSample cancel =
      lastPointerSample_.value_or(input::PointerSample{});
  cancel.pointerId = pointerId;
  cancel.phase = input::PointerPhase::Cancel;
  return onPointerSample(cancel);
}

HRESULT WhiteboardApp::onPointerSamples(
    std::vector<input::PointerSample> samples) {
  batchingPointerSamples_ = true;
  batchedDirtyBounds_.reset();
  batchedFullRedraw_ = false;
  if (activeStroke_) batchedLayer_ = activeDocumentLayer();
  HRESULT result = S_OK;
  for (const auto& sample : samples) {
    result = onPointerSample(sample);
    if (FAILED(result)) break;
  }
  batchingPointerSamples_ = false;
  if (FAILED(result)) return result;
  if (batchedFullRedraw_) {
    return (batchedLayer_ == document::LayerClass::Base ? baseLayer_
                                                          : annotationLayer_)
        .render(document_, batchedLayer_);
  }
  if (!batchedDirtyBounds_) return result;
  return (batchedLayer_ == document::LayerClass::Base ? baseLayer_
                                                        : annotationLayer_)
      .render(document_, batchedLayer_, batchedDirtyBounds_);
}

HRESULT WhiteboardApp::onPointerSample(const input::PointerSample& sample) {
  if (sample.phase == input::PointerPhase::Down) {
    if (activeStroke_) return S_FALSE;
    const auto hit = hitEmbedded(sample.screenPosition);
    inputRouter_.setActiveEmbeddedNode(hit);
    activeRoute_ = inputRouter_.route(sample.kind, hit);
    batchedLayer_ = activeDocumentLayer();
    if (activeRoute_.target != input::InputTarget::BaseCanvas &&
        activeRoute_.target != input::InputTarget::Annotation) {
      return S_FALSE;
    }
    activeStroke_.emplace(4.0F);
    activePointerId_ = sample.pointerId;
    lastPointerSample_ = sample;
    activeStroke_->begin(sample);
    document::StrokeNode preview;
    preview.width = 4.0F;
    preview.points.push_back(document::StrokePoint{
        sample.screenPosition, sample.pressure, sample.timestampMicros});
    activeStrokeId_ = "stroke-" + std::to_string(++strokeSerial_);
    document::Node previewNode;
    previewNode.id = activeStrokeId_;
    previewNode.layer = activeDocumentLayer();
    previewNode.bounds =
        core::Rect{sample.screenPosition.x, sample.screenPosition.y, 0, 0}
            .inflated(2.0F);
    previewNode.payload = std::move(preview);
    if (!document_.add(std::move(previewNode))) return E_FAIL;
    return S_OK;
  }
  if (!activeStroke_ || activePointerId_ != sample.pointerId) return S_FALSE;
  lastPointerSample_ = sample;
  if (sample.phase == input::PointerPhase::Move) {
    const stroke::StrokeUpdate update = activeStroke_->append(sample);
    if (update.accepted) {
      if (!document_.appendStrokePoint(
              activeStrokeId_,
              document::StrokePoint{sample.screenPosition, sample.pressure,
                                    sample.timestampMicros},
              update.dirtyBounds)) {
        return E_FAIL;
      }
    }
    if (update.dirtyBounds.width > 0.0F && update.dirtyBounds.height > 0.0F) {
      if (batchingPointerSamples_) {
        batchedDirtyBounds_ = batchedDirtyBounds_
                                  ? batchedDirtyBounds_->united(update.dirtyBounds)
                                  : std::optional<core::Rect>(update.dirtyBounds);
      } else {
        const HRESULT hr = activeSwapChainLayer().render(
            document_, activeDocumentLayer(), update.dirtyBounds);
        if (FAILED(hr)) return hr;
      }
    }
    return S_OK;
  }
  if (sample.phase == input::PointerPhase::Cancel) {
    const document::LayerClass layer = activeDocumentLayer();
    SkiaSwapChainLayer& swapChainLayer = activeSwapChainLayer();
    swapChainLayer.invalidateNode(activeStrokeId_);
    document_.erase(activeStrokeId_);
    activeStroke_.reset();
    activePointerId_.reset();
    activeStrokeId_.clear();
    lastPointerSample_.reset();
    if (batchingPointerSamples_) {
      batchedLayer_ = layer;
      batchedFullRedraw_ = true;
      return S_OK;
    }
    return swapChainLayer.render(document_, layer);
  }
  const stroke::StrokeUpdate finalUpdate = activeStroke_->append(sample);
  document::StrokeNode finished = activeStroke_->finish();
  const core::Rect finalDirty = activeStroke_->finishDirtyBounds();
  HRESULT completionResult = S_OK;
  std::optional<document::NodeId> storedParent;
  if (activeRoute_.parentId) {
    const document::Node* parent = document_.find(*activeRoute_.parentId);
    if (parent == nullptr) {
      completionResult = E_FAIL;
    } else {
      try {
        finished = document::attachStrokeToParent(finished, parent->bounds);
        storedParent = activeRoute_.parentId;
      } catch (const std::domain_error&) {
        completionResult = E_FAIL;
      }
    }
  }
  const bool stored = document_.mutate(
      activeStrokeId_, [&](document::Node& node) {
        node.payload = std::move(finished);
        node.parentId = storedParent;
        if (finalUpdate.dirtyBounds.width > 0.0F &&
            finalUpdate.dirtyBounds.height > 0.0F) {
          node.bounds = node.bounds.united(finalUpdate.dirtyBounds);
        }
        if (finalDirty.width > 0.0F && finalDirty.height > 0.0F) {
          node.bounds = node.bounds.united(finalDirty);
        }
      });
  if (!stored) completionResult = E_FAIL;
  const document::LayerClass layer = activeDocumentLayer();
  SkiaSwapChainLayer& swapChainLayer = activeSwapChainLayer();
  swapChainLayer.invalidateNode(activeStrokeId_);
  activeStroke_.reset();
  activePointerId_.reset();
  activeStrokeId_.clear();
  lastPointerSample_.reset();
  std::optional<core::Rect> redraw;
  if (finalUpdate.dirtyBounds.width > 0.0F &&
      finalUpdate.dirtyBounds.height > 0.0F) {
    redraw = finalUpdate.dirtyBounds;
  }
  if (finalDirty.width > 0.0F && finalDirty.height > 0.0F) {
    redraw = redraw ? redraw->united(finalDirty) : finalDirty;
  }
  if (batchingPointerSamples_) {
    batchedLayer_ = layer;
    if (redraw) {
      batchedDirtyBounds_ = batchedDirtyBounds_
                                ? batchedDirtyBounds_->united(*redraw)
                                : redraw;
    } else {
      batchedFullRedraw_ = true;
    }
    return completionResult;
  }
  const HRESULT renderResult = swapChainLayer.render(document_, layer, redraw);
  return FAILED(renderResult) ? renderResult : completionResult;
}

bool WhiteboardApp::populateSelfTestDocument() {
  auto addStroke = [this](std::string id, document::LayerClass layer,
                          std::uint32_t color,
                          std::initializer_list<core::Vec2> positions,
                          float width = 8.0F) {
    document::StrokeNode stroke;
    stroke.colorArgb = color;
    stroke.width = width;
    for (const auto position : positions) {
      stroke.points.push_back(document::StrokePoint{position, 1.0F, 0});
    }
    document::Node node;
    node.id = std::move(id);
    node.layer = layer;
    node.payload = std::move(stroke);
    return document_.add(std::move(node));
  };

  document::Node embedded;
  embedded.id = "self-test-embedded";
  embedded.layer = document::LayerClass::Embedded;
  embedded.bounds = core::Rect{440.0F, 240.0F, 400.0F, 240.0F};
  embedded.payload = document::EmbeddedNode{
      document::EmbeddedKind::Web, "https://example.com/",
      "Embedded placeholder"};
  if (!document_.add(std::move(embedded))) return false;
  for (std::size_t row = 0; row < 10; ++row) {
    const float y = 260.0F + static_cast<float>(row) * 22.0F;
    if (!addStroke("self-test-embedded-fill-" + std::to_string(row),
                   document::LayerClass::Embedded, 0xFFDDDDDDU,
                   {{460, y}, {820, y}}, 24.0F)) {
      return false;
    }
  }
  if (!addStroke("self-test-embedded-outline", document::LayerClass::Embedded,
                 0xFF666666U,
                 {{440, 240}, {840, 240}, {840, 480}, {440, 480}, {440, 240}},
                 12.0F)) {
    return false;
  }
  if (!addStroke("self-test-annotation", document::LayerClass::Annotation,
                 0xFFFF0000U, {{430, 230}, {850, 490}}, 12.0F) ||
      !addStroke("self-test-annotation-cross", document::LayerClass::Annotation,
                 0xFFFF0000U, {{850, 230}, {430, 490}}, 12.0F)) {
    return false;
  }
  constexpr core::Vec2 handles[]{{430, 230}, {850, 230}, {850, 490},
                                 {430, 490}};
  for (std::size_t index = 0; index < 4; ++index) {
    const auto p = handles[index];
    if (!addStroke("self-test-handle-" + std::to_string(index),
                   document::LayerClass::Chrome, 0xFF0066FFU,
                   {{p.x - 10, p.y}, {p.x + 10, p.y},
                    {p.x, p.y - 10}, {p.x, p.y + 10}},
                   8.0F)) {
      return false;
    }
  }
  return true;
}

bool WhiteboardApp::populateEmbeddedSelfTestDocument() {
  const auto addEmbedded = [this](document::NodeId id,
                                  document::EmbeddedKind kind,
                                  core::Rect bounds, std::string source,
                                  std::string title) {
    document::Node node;
    node.id = std::move(id);
    node.layer = document::LayerClass::Embedded;
    node.bounds = bounds;
    node.payload = document::EmbeddedNode{
        kind, std::move(source), std::move(title)};
    return document_.add(std::move(node));
  };
  const auto addStroke =
      [this](document::NodeId id, document::LayerClass layer,
             std::uint32_t color,
             std::initializer_list<core::Vec2> positions,
             float width = 8.0F) {
        if (positions.size() == 0) return false;
        document::StrokeNode stroke;
        stroke.colorArgb = color;
        stroke.width = width;
        auto position = positions.begin();
        float minX = position->x;
        float minY = position->y;
        float maxX = position->x;
        float maxY = position->y;
        for (const auto point : positions) {
          stroke.points.push_back(document::StrokePoint{point, 1.0F, 0});
          minX = (std::min)(minX, point.x);
          minY = (std::min)(minY, point.y);
          maxX = (std::max)(maxX, point.x);
          maxY = (std::max)(maxY, point.y);
        }
        document::Node node;
        node.id = std::move(id);
        node.layer = layer;
        node.bounds = core::Rect{minX, minY, maxX - minX, maxY - minY}
                          .inflated(width * 0.5F);
        node.payload = std::move(stroke);
        return document_.add(std::move(node));
      };

  constexpr core::Rect kRichTextBounds{40.0F, 100.0F, 360.0F, 240.0F};
  constexpr core::Rect kVideoBounds{440.0F, 100.0F, 400.0F, 225.0F};
  constexpr core::Rect kWebBounds{880.0F, 100.0F, 360.0F, 240.0F};
  if (!addEmbedded("rich-text-1", document::EmbeddedKind::RichText,
                   kRichTextBounds, "https://canvas.local/richtext.html",
                   "Rich text") ||
      !addEmbedded("video-1", document::EmbeddedKind::Video, kVideoBounds,
                   "https://canvas.local/video.html", "HTML video") ||
      !addEmbedded("web-1", document::EmbeddedKind::Web, kWebBounds,
                   "https://example.com/", "HTTPS web page")) {
    return false;
  }

  constexpr core::Rect kSurfaces[]{kRichTextBounds, kVideoBounds, kWebBounds};
  constexpr const char* kSurfaceIds[]{"rich-text-1", "video-1", "web-1"};
  for (std::size_t index = 0; index < std::size(kSurfaces); ++index) {
    const core::Rect bounds = kSurfaces[index];
    const core::Vec2 annotationStart{bounds.x + 20.0F, bounds.y + 20.0F};
    const core::Vec2 annotationEnd{bounds.x + bounds.width - 20.0F,
                                   bounds.y + bounds.height - 20.0F};
    document::StrokeNode attachedStroke;
    attachedStroke.colorArgb = 0xFFFF2020U;
    attachedStroke.width = 10.0F;
    attachedStroke.points = {{annotationStart, 1.0F, 0},
                             {annotationEnd, 1.0F, 0}};
    try {
      attachedStroke =
          document::attachStrokeToParent(std::move(attachedStroke), bounds);
    } catch (const std::domain_error&) {
      return false;
    }
    document::Node annotation;
    annotation.id = "embedded-annotation-" + std::to_string(index);
    annotation.layer = document::LayerClass::Annotation;
    annotation.bounds =
        core::Rect::fromPoints(annotationStart, annotationEnd).inflated(5.0F);
    annotation.parentId = kSurfaceIds[index];
    annotation.payload = std::move(attachedStroke);
    if (!document_.add(std::move(annotation))) return false;
    constexpr float kHandleRadius = 10.0F;
    const core::Vec2 handle{bounds.x + bounds.width,
                            bounds.y + bounds.height};
    if (!addStroke("embedded-handle-" + std::to_string(index),
                   document::LayerClass::Chrome, 0xFF0066FFU,
                   {{handle.x - kHandleRadius, handle.y},
                    {handle.x + kHandleRadius, handle.y},
                    {handle.x, handle.y - kHandleRadius},
                    {handle.x, handle.y + kHandleRadius}},
                   8.0F)) {
      return false;
    }
  }
  return true;
}

HRESULT WhiteboardApp::openDocument(const std::wstring& path,
                                    std::string_view requestId,
                                    NamedPipeServer::ConnectionId connectionId) {
  std::vector<std::uint8_t> bytes;
  std::string storeError;
  if (!DocumentStore::load(std::filesystem::path(path), bytes, storeError)) {
    if (!storeError.empty()) {
      OutputDebugStringA(("Canvas document load failed: " + storeError +
                          "\n")
                             .c_str());
    }
    return HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
  }
  std::string decodeError;
  document::Document candidate;
  if (!storage::DocumentCodec::decodeInto(bytes, candidate, decodeError)) {
    if (!decodeError.empty()) {
      OutputDebugStringA(("Canvas document decode failed: " + decodeError +
                          "\n")
                             .c_str());
    }
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
  }

  // Startup has no composition tree yet. The decoded candidate is still
  // atomic, and run() restores its surfaces after the UI is initialized.
  if (window_ == nullptr) {
    document_ = std::move(candidate);
    return S_OK;
  }

  // Keep the current document and visible surfaces untouched while the
  // candidate's hidden WebViews report their initial-load terminal states.
  cancelPendingOpen();
  pendingOpenNotificationFailure_.reset();
  const HRESULT inputCancelResult = cancelActiveInputForDocumentTransition();
  if (FAILED(inputCancelResult)) return inputCancelResult;
  if (nextDocumentGeneration_ == 0U ||
      nextDocumentGeneration_ == (std::numeric_limits<std::uint64_t>::max)())
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
  auto pending = std::make_unique<PendingOpen>();
  pending->candidate = std::move(candidate);
  pending->generation = nextDocumentGeneration_++;
  pending->requestId = std::string(requestId);
  pending->connectionId = connectionId;
  pending->callbackState = std::make_shared<EmbeddedLoadCallbackState>();
  pending->callbackState->owner = this;
  pendingOpen_ = std::move(pending);
  openDocumentResponsePending_ = true;
  openDocumentResponseSent_ = false;
  if (embeddedCompletionTimeoutTimerId_ ==
      (std::numeric_limits<UINT_PTR>::max)()) {
    embeddedCompletionTimeoutTimerId_ = kEmbeddedCompletionTimeoutTimer;
  } else {
    ++embeddedCompletionTimeoutTimerId_;
  }
  if (SetTimer(window_, embeddedCompletionTimeoutTimerId_,
               kEmbeddedCompletionTimeoutMs, nullptr) == 0U) {
    const DWORD timerError = GetLastError();
    const auto failureGeneration = pendingOpen_->generation;
    pendingOpen_->callbackState->active = false;
    pendingOpen_->callbackState->owner = nullptr;
    (void)embeddedLoadCompletions_.cancelGeneration(failureGeneration);
    (void)embeddedLoadTracker_.cancelGeneration(failureGeneration);
    pendingOpen_.reset();
    openDocumentResponsePending_ = false;
    return timerError == ERROR_SUCCESS ? E_FAIL
                                       : HRESULT_FROM_WIN32(timerError);
  }
  stagingPendingOpen_ = true;
  HRESULT result = restoreEmbeddedSurfaces(
      pendingOpen_->candidate, pendingOpen_->surfaces, false, pendingOpen_.get());
  stagingPendingOpen_ = false;
  if (pendingOpenNotificationFailure_) {
    result = *pendingOpenNotificationFailure_;
    pendingOpenNotificationFailure_.reset();
  }
  if (FAILED(result)) {
    const auto failureGeneration = pendingOpen_->generation;
    pendingOpen_->callbackState->active = false;
    pendingOpen_->callbackState->owner = nullptr;
    (void)KillTimer(window_, embeddedCompletionTimeoutTimerId_);
    (void)embeddedLoadCompletions_.cancelGeneration(failureGeneration);
    (void)embeddedLoadTracker_.cancelGeneration(failureGeneration);
    pendingOpen_.reset();
    openDocumentResponsePending_ = false;
    return result;
  }
  pendingOpen_->batch = canvas::app::EmbeddedLoadBatch::create(
      pendingOpen_->generation, std::move(pendingOpen_->loads));
  if (!pendingOpen_->batch) {
    const auto failureGeneration = pendingOpen_->generation;
    pendingOpen_->callbackState->active = false;
    pendingOpen_->callbackState->owner = nullptr;
    (void)KillTimer(window_, embeddedCompletionTimeoutTimerId_);
    (void)embeddedLoadCompletions_.cancelGeneration(failureGeneration);
    (void)embeddedLoadTracker_.cancelGeneration(failureGeneration);
    pendingOpen_.reset();
    openDocumentResponsePending_ = false;
    return E_OUTOFMEMORY;
  }
  // The response acknowledges synchronous staging admission only. The
  // document-state event below is emitted by commitPendingOpen after every
  // embedded surface reaches its terminal Ready state.
  sendOpenDocumentAdmission();
  if (pendingOpen_->batch->state() ==
      canvas::app::EmbeddedLoadBatch::State::Ready) {
    commitPendingOpen();
  }
  return S_OK;
}

HRESULT WhiteboardApp::saveDocument(const std::wstring& path) const {
  // Copy before encoding so serialization never observes a document that is
  // being changed by a pointer callback.
  const document::Document snapshot = document_;
  const std::vector<std::uint8_t> bytes =
      storage::DocumentCodec::encode(snapshot);
  if (bytes.empty()) {
    OutputDebugStringA("Canvas document encode produced no bytes\n");
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
  }
  std::string storeError;
  if (!DocumentStore::saveAtomic(std::filesystem::path(path), bytes,
                                 storeError)) {
    if (!storeError.empty()) {
      OutputDebugStringA(("Canvas document save failed: " + storeError +
                          "\n")
                             .c_str());
    }
    return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
  }
  return S_OK;
}

HRESULT WhiteboardApp::restoreEmbeddedSurfaces(
    const document::Document& source, std::vector<HostedWebView>& destinations,
    bool visible, PendingOpen* pending) {
  if (!destinations.empty()) return E_INVALIDARG;
  std::size_t embeddedCount = 0U;
  for (const document::Node& node : source.nodes()) {
    if (node.layer != document::LayerClass::Embedded ||
        !std::holds_alternative<document::EmbeddedNode>(node.payload)) {
      continue;
    }
    if (!canvas::app::EmbeddedLoadBatch::isLoadCountWithinLimit(
            embeddedCount + 1U)) {
      return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    }
    ++embeddedCount;
  }
  for (const document::Node& node : source.nodes()) {
    if (node.layer != document::LayerClass::Embedded ||
        !std::holds_alternative<document::EmbeddedNode>(node.payload)) {
      continue;
    }
    const HRESULT result = createEmbeddedSurface(node, destinations, visible, pending);
    if (FAILED(result)) {
      destinations.clear();
      return result;
    }
  }
  return S_OK;
}

HRESULT WhiteboardApp::createEmbeddedSurface(
    const document::Node& node, std::vector<HostedWebView>& destinations,
    bool visible, PendingOpen* pending) {
  if (window_ == nullptr || node.id.empty() || node.id.size() > 1024U ||
      !isValidEmbeddedBounds(node.bounds) ||
      node.layer != document::LayerClass::Embedded) {
    return E_INVALIDARG;
  }
  const auto* embedded = std::get_if<document::EmbeddedNode>(&node.payload);
  if (embedded == nullptr || embedded->source.empty() ||
      embedded->source.size() > kMaximumEmbeddedSourceBytes) {
    return E_INVALIDARG;
  }
  const auto source = utf8ToWide(embedded->source);
  const auto wideNodeId = utf8ToWide(node.id);
  if (!source || !wideNodeId || source->empty() || wideNodeId->empty() ||
      wideNodeId->size() > 256U) {
    return E_INVALIDARG;
  }

  WebView2Surface::Options options;
  std::wstring navigationUri = *source;
  std::optional<std::wstring> initialMessage;
  const auto usesCanvasLocal = [&source] {
    constexpr std::wstring_view kPrefix = L"https://canvas.local/";
    return source->size() >= kPrefix.size() &&
           _wcsnicmp(source->c_str(), kPrefix.data(), kPrefix.size()) == 0;
  };

  switch (embedded->kind) {
    case document::EmbeddedKind::Web: {
      const auto navigation =
          WebView2Surface::classifyNavigation(*source, false);
      if (navigation == WebView2Surface::NavigationClass::Https) {
        break;
      }
      if (navigation == WebView2Surface::NavigationClass::LocalVirtualHost &&
          usesCanvasLocal()) {
        options.canvasLocalFolder = L"web";
        break;
      }
      return E_ACCESSDENIED;
    }
    case document::EmbeddedKind::RichText: {
      constexpr std::wstring_view kRichTextPage =
          L"https://canvas.local/richtext.html";
      if (source->size() < kRichTextPage.size() ||
          _wcsnicmp(source->c_str(), kRichTextPage.data(),
                    kRichTextPage.size()) != 0 ||
          (source->size() > kRichTextPage.size() &&
           (*source)[kRichTextPage.size()] != L'?') ||
          !usesCanvasLocal()) {
        return E_ACCESSDENIED;
      }
      options.canvasLocalFolder = L"web";
      const auto encodedNodeId = percentEncodeQueryComponent(*wideNodeId);
      if (!encodedNodeId) return E_INVALIDARG;
      navigationUri = L"https://canvas.local/richtext.html?nodeId=" +
                      *encodedNodeId;
      break;
    }
    case document::EmbeddedKind::Video: {
      const auto plan = detail::buildVideoRestorePlan(*wideNodeId, *source);
      if (!plan) return E_INVALIDARG;
      options.canvasLocalFolder = L"web";
      navigationUri = plan->navigationUri;
      if (videoPath_) {
        detail::LocalMediaSource mediaSource;
        const HRESULT result =
            detail::approveLocalMediaFile(*videoPath_, mediaSource);
        if (FAILED(result)) return result;
        options.mediaCanvasLocalFolder = mediaSource.folder;
        initialMessage =
            detail::buildSetVideoSourceMessage(*wideNodeId, mediaSource.uri);
      } else {
        switch (plan->mediaAction) {
          case detail::VideoRestoreMediaAction::None:
            break;
          case detail::VideoRestoreMediaAction::UsePersistedRemote:
            initialMessage = plan->initialMessage;
            break;
          case detail::VideoRestoreMediaAction::ApprovePersistedLocalFile: {
            detail::LocalMediaSource mediaSource;
            const HRESULT result =
                detail::approveLocalMediaFile(*source, mediaSource);
            if (FAILED(result)) return result;
            options.mediaCanvasLocalFolder = mediaSource.folder;
            initialMessage = detail::buildSetVideoSourceMessage(*wideNodeId,
                                                                 mediaSource.uri);
            break;
          }
          case detail::VideoRestoreMediaAction::Reject:
            return E_ACCESSDENIED;
        }
      }
      if ((plan->mediaAction != detail::VideoRestoreMediaAction::None ||
           videoPath_) && !initialMessage) {
        return E_INVALIDARG;
      }
      break;
    }
  }

  auto surface = std::make_unique<WebView2Surface>(composition_, window_,
                                                   std::move(options));
  std::optional<canvas::app::EmbeddedLoadBatch::Token> loadToken;
  if (pending != nullptr) {
    loadToken = embeddedLoadTracker_.begin(
        node.id, pending->requestId, pending->connectionId,
        pending->generation);
    if (!loadToken) return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    try {
      pending->loads.push_back({*loadToken, node.id});
    } catch (...) {
      (void)embeddedLoadTracker_.cancel(*loadToken);
      return E_OUTOFMEMORY;
    }
  }
  const auto abandonToken = [&]() noexcept {
    if (loadToken) (void)embeddedLoadTracker_.cancel(*loadToken);
  };
  HRESULT result = setSurfaceBounds(*surface, node.bounds);
  if (FAILED(result)) { abandonToken(); return result; }
  surface->setInteractive(false);
  result = setSurfaceVisible(*surface, visible);
  if (FAILED(result)) { abandonToken(); return result; }
  result = surface->initialize();
  if (SUCCEEDED(result) && pending != nullptr) {
    const auto weakState = std::weak_ptr<EmbeddedLoadCallbackState>(
        pending->callbackState);
    result = surface->setInitialLoadCompletionHandler(
        [weakState, token = *loadToken, generation = pending->generation](
            WebView2Surface::InitialLoadCompletion completion) {
          const auto state = weakState.lock();
          if (!state || !state->active || state->owner == nullptr) return;
          state->owner->enqueueEmbeddedLoadCompletion(generation, token,
                                                       completion);
        });
  }
  if (SUCCEEDED(result)) result = surface->navigate(navigationUri);
  if (SUCCEEDED(result) && initialMessage) result = surface->postMessage(*initialMessage);
  if (FAILED(result)) { abandonToken(); return result; }
  if (pending != nullptr && pendingOpenNotificationFailure_) {
    const HRESULT notificationFailure = *pendingOpenNotificationFailure_;
    abandonToken();
    return notificationFailure;
  }
  try {
    destinations.push_back(HostedWebView{node.id, std::move(surface)});
  } catch (...) {
    abandonToken();
    return E_OUTOFMEMORY;
  }
  return S_OK;
}

HRESULT WhiteboardApp::setSurfaceBounds(WebView2Surface& surface,
                                        core::Rect bounds) {
  if (!isValidEmbeddedBounds(bounds)) return E_INVALIDARG;
  return surface.setBoundsChecked(bounds);
}

HRESULT WhiteboardApp::setSurfaceVisible(WebView2Surface& surface,
                                         bool visible) {
  return surface.setVisibleChecked(visible);
}

bool WhiteboardApp::restorePreviousRender(
    const document::Document& previous, std::string_view requestId,
    std::string_view context, NamedPipeServer::ConnectionId connectionId) {
  const HRESULT result = renderDocument(previous);
  if (SUCCEEDED(result)) return true;
  reportFatalFailure(result, requestId, context, connectionId);
  return false;
}

void WhiteboardApp::reportFatalFailure(HRESULT result,
                                       std::string_view requestId,
                                       std::string_view context,
                                       NamedPipeServer::ConnectionId connectionId) {
  if (SUCCEEDED(result)) result = E_FAIL;
  lastError_ = result;
  const std::string detail(context);
  OutputDebugStringA(("Canvas fatal failure: " + detail + "\n").c_str());
  sendIpc(ipc::Message{
      1, "fatal-error",
      "fatal-" + (requestId.empty() ? std::string("native")
                                     : std::string(requestId)),
      nlohmann::json{{"error", detail},
                     {"hresult", static_cast<std::int64_t>(result)}}},
      connectionId);
  if (window_ != nullptr && IsWindow(window_)) {
    (void)PostMessageW(window_, WM_CLOSE, 0, 0);
  }
}

void WhiteboardApp::handleIpcMessages() {
  if (FAILED(lastError_)) return;
  constexpr std::size_t kMaximumMessagesPerUiTurn = 16U;
  std::deque<QueuedIpcMessage> messages;
  bool repost = false;
  {
    std::lock_guard<std::mutex> lock(ipcMessagesMutex_);
    for (std::size_t index = 0;
         index < kMaximumMessagesPerUiTurn && !ipcMessages_.empty(); ++index) {
      const auto bytes = queuedIpcMessageBytes(ipcMessages_.front().message);
      if (bytes && *bytes <= ipcQueuedBytes_) {
        ipcQueuedBytes_ -= *bytes;
      } else {
        // The queue only receives messages with a recorded finite budget.
        // Keep draining if a future change violates that invariant.
        ipcQueuedBytes_ = 0;
      }
      messages.push_back(std::move(ipcMessages_.front()));
      ipcMessages_.pop_front();
    }
    if (ipcMessages_.empty()) {
      ipcMessagePosted_ = false;
    } else {
      // The message that invoked this handler is now consumed. Reserve exactly
      // one successor so a busy pipe cannot flood the UI message queue.
      ipcMessagePosted_ = true;
      repost = true;
    }
  }
  for (const auto& queued : messages) {
    if (ipcServer_ && ipcServer_->isCurrentConnection(queued.connectionId)) {
      handleIpcMessage(queued.message, queued.connectionId);
      // A failed rollback has already queued WM_CLOSE. Do not let later
      // commands observe or mutate unrecoverable state before destruction.
      if (FAILED(lastError_)) return;
    }
  }
  // Commands processed above may be slow enough for the pipe to enqueue more
  // work after the first check. Recheck only when no successor was reserved.
  if (!repost) {
    std::lock_guard<std::mutex> lock(ipcMessagesMutex_);
    if (!ipcMessages_.empty() && !ipcMessagePosted_) {
      ipcMessagePosted_ = true;
      repost = true;
    }
  }
  if (repost && window_ != nullptr) {
    (void)PostMessageW(window_, kIpcMessage, 0, 0);
  }
}

void WhiteboardApp::enqueueEmbeddedLoadCompletion(
    std::uint64_t generation, canvas::app::EmbeddedLoadBatch::Token token,
    WebView2Surface::InitialLoadCompletion completion) {
  if (closing_ || !pendingOpen_ || pendingOpen_->generation != generation) return;
  const auto outcome = completion.state == WebView2Surface::InitialLoadState::Ready
                           ? canvas::app::EmbeddedLoadCompletionInbox::Outcome::Ready
                           : canvas::app::EmbeddedLoadCompletionInbox::Outcome::Failed;
  std::int32_t failure = 0;
  if (outcome == canvas::app::EmbeddedLoadCompletionInbox::Outcome::Failed) {
    failure = static_cast<std::int32_t>(completion.result);
    if (failure >= 0) failure = -1;
  }
  const auto queued = embeddedLoadCompletions_.enqueue(
      {token, generation, outcome, failure});
  if (queued.status == canvas::app::EmbeddedLoadCompletionInbox::EnqueueStatus::Invalid ||
      queued.status == canvas::app::EmbeddedLoadCompletionInbox::EnqueueStatus::Full) {
    if (stagingPendingOpen_) {
      pendingOpenNotificationFailure_ = E_OUTOFMEMORY;
    } else {
      failPendingOpen(E_OUTOFMEMORY, "embedded completion inbox overflow");
    }
    return;
  }
  if (queued.shouldPostNotification) {
    embeddedCompletionMessagePosted_ = true;
    if (window_ == nullptr ||
        !PostMessageW(window_, kEmbeddedCompletionMessage, 0, 0)) {
      embeddedCompletionMessagePosted_ = false;
      (void)embeddedLoadCompletions_.notificationPostFailed();
      const DWORD postError = GetLastError();
      const HRESULT failure = postError == ERROR_SUCCESS
                                  ? E_FAIL
                                  : HRESULT_FROM_WIN32(postError);
      if (stagingPendingOpen_) {
        pendingOpenNotificationFailure_ = failure;
      } else {
        failPendingOpen(failure, "embedded completion notification failed");
      }
    }
  }
}

void WhiteboardApp::handleEmbeddedLoadCompletions() {
  embeddedCompletionMessagePosted_ = false;
  if (closing_) return;
  (void)embeddedLoadCompletions_.consumeNotification();
  constexpr std::size_t kMaximumEventsPerUiTurn = 16U;
  for (std::size_t index = 0; index < kMaximumEventsPerUiTurn; ++index) {
    canvas::app::EmbeddedLoadCompletionInbox::Event event;
    if (!embeddedLoadCompletions_.pop(event)) break;
    if (!pendingOpen_ || event.documentGeneration != pendingOpen_->generation) continue;
    const auto record = embeddedLoadTracker_.consume(event.token,
                                                      event.documentGeneration);
    if (!record || !pendingOpen_->batch) continue;
    const auto completion = event.outcome ==
                                    canvas::app::EmbeddedLoadCompletionInbox::Outcome::Ready
                                ? canvas::app::EmbeddedLoadBatch::Completion::Ready
                                : canvas::app::EmbeddedLoadBatch::Completion::Failed;
    if (!pendingOpen_->batch->complete(event.token, event.documentGeneration,
                                       completion))
      continue;
    if (completion == canvas::app::EmbeddedLoadBatch::Completion::Failed) {
      failPendingOpen(static_cast<HRESULT>(event.failureCode),
                      "embedded surface initial load failed");
      return;
    }
    if (pendingOpen_ && pendingOpen_->batch->state() ==
                            canvas::app::EmbeddedLoadBatch::State::Ready) {
      commitPendingOpen();
      return;
    }
  }
  if (!pendingOpen_ || embeddedLoadCompletions_.empty()) return;
  if (embeddedLoadCompletions_.requestNotificationIfNeeded()) {
    embeddedCompletionMessagePosted_ = true;
    if (window_ == nullptr ||
        !PostMessageW(window_, kEmbeddedCompletionMessage, 0, 0)) {
      embeddedCompletionMessagePosted_ = false;
      (void)embeddedLoadCompletions_.notificationPostFailed();
      const DWORD postError = GetLastError();
      const HRESULT failure = postError == ERROR_SUCCESS
                                  ? E_FAIL
                                  : HRESULT_FROM_WIN32(postError);
      failPendingOpen(failure, "embedded completion notification failed");
    }
  }
}

void WhiteboardApp::sendOpenDocumentAdmission() {
  if (!pendingOpen_ || openDocumentResponseSent_) return;
  const auto requestId = pendingOpen_->requestId;
  const auto connectionId = pendingOpen_->connectionId;
  openDocumentResponsePending_ = false;
  openDocumentResponseSent_ = true;
  sendIpc(ipc::Message{1, "response", requestId,
                       nlohmann::json{{"accepted", true}}},
          connectionId);
}

HRESULT WhiteboardApp::cancelActiveInputForDocumentTransition() {
  HRESULT firstFailure = S_OK;
  const auto rememberFailure = [&firstFailure](HRESULT result) {
    if (FAILED(result) && SUCCEEDED(firstFailure)) firstFailure = result;
  };
  if (activePointerId_) {
    rememberFailure(cancelActivePointer(*activePointerId_));
  }
  if (activeEmbeddedPointerId_) {
    rememberFailure(cancelEmbeddedTouch(*activeEmbeddedPointerId_));
  }
  if (embeddedMouseNodeId_ || embeddedMouseSession_.buttons() != 0 ||
      embeddedMouseSession_.hovered()) {
    rememberFailure(forwardMouseToEmbedded(window_, WM_CANCELMODE, 0, 0));
  }
  // These identifiers describe the old document. Never let a late pointer
  // message select a node in the newly committed candidate.
  activePointerId_.reset();
  activeStroke_.reset();
  activeStrokeId_.clear();
  lastPointerSample_.reset();
  activeEmbeddedPointerId_.reset();
  activeEmbeddedTouchNodeId_.reset();
  embeddedMouseNodeId_.reset();
  (void)embeddedMouseSession_.disable();
  if (window_ != nullptr && GetCapture() == window_) ReleaseCapture();
  inputRouter_.setActiveEmbeddedNode(std::nullopt);
  activeRoute_ = input::RouteResult{};
  return firstFailure;
}

void WhiteboardApp::commitPendingOpen() {
  if (closing_ || !pendingOpen_ || !pendingOpen_->batch ||
      pendingOpen_->batch->state() != canvas::app::EmbeddedLoadBatch::State::Ready)
    return;
  const auto requestId = pendingOpen_->requestId;
  const auto connectionId = pendingOpen_->connectionId;
  const auto nodeCount = pendingOpen_->candidate.nodes().size();
  const HRESULT inputCancelResult = cancelActiveInputForDocumentTransition();
  if (FAILED(inputCancelResult)) {
    reportFatalFailure(inputCancelResult, requestId,
                       "open-document active input cancellation failed",
                       connectionId);
    failPendingOpen(inputCancelResult,
                    "could not cancel active input for document commit");
    return;
  }
  HRESULT result = renderDocument(pendingOpen_->candidate);
  if (FAILED(result)) {
    if (!restorePreviousRender(document_, requestId,
                               "open-document render rollback failed",
                               connectionId)) {
      failPendingOpen(lastError_, "open-document render rollback failed");
      return;
    }
    failPendingOpen(result, "could not render candidate document");
    return;
  }
  for (const auto& hosted : pendingOpen_->surfaces) {
    result = setSurfaceVisible(*hosted.surface, true);
    if (FAILED(result)) {
      for (const auto& shown : pendingOpen_->surfaces)
        (void)setSurfaceVisible(*shown.surface, false);
      if (!restorePreviousRender(document_, requestId,
                                 "open-document visibility rollback failed",
                                 connectionId)) {
        failPendingOpen(lastError_, "open-document visibility rollback failed");
        return;
      }
      failPendingOpen(result, "could not show candidate embedded surface");
      return;
    }
  }
  pendingOpen_->callbackState->active = false;
  pendingOpen_->callbackState->owner = nullptr;
  stagingPendingOpen_ = false;
  pendingOpenNotificationFailure_.reset();
  (void)KillTimer(window_, embeddedCompletionTimeoutTimerId_);
  (void)embeddedLoadCompletions_.cancelGeneration(pendingOpen_->generation);
  (void)embeddedLoadTracker_.cancelGeneration(pendingOpen_->generation);
  document_ = std::move(pendingOpen_->candidate);
  embeddedWebViews_.swap(pendingOpen_->surfaces);
  pendingOpen_.reset();
  openDocumentResponsePending_ = false;
  openDocumentResponseSent_ = true;
  sendIpc(ipc::Message{1, "document-state", "state-" + requestId,
                       nlohmann::json{{"nodeCount", nodeCount}}},
          connectionId);
}

void WhiteboardApp::failPendingOpen(HRESULT result, std::string_view reason) {
  if (!pendingOpen_) return;
  const auto requestId = pendingOpen_->requestId;
  const auto connectionId = pendingOpen_->connectionId;
  const bool admissionSent = openDocumentResponseSent_;
  pendingOpen_->callbackState->active = false;
  pendingOpen_->callbackState->owner = nullptr;
  stagingPendingOpen_ = false;
  pendingOpenNotificationFailure_.reset();
  (void)KillTimer(window_, embeddedCompletionTimeoutTimerId_);
  (void)embeddedLoadCompletions_.cancelGeneration(pendingOpen_->generation);
  (void)embeddedLoadTracker_.cancelGeneration(pendingOpen_->generation);
  pendingOpen_.reset();
  openDocumentResponsePending_ = false;
  openDocumentResponseSent_ = true;
  if (!closing_) {
    if (admissionSent) {
      sendIpc(ipc::Message{
                  1, "diagnostics", requestId,
                  nlohmann::json{{"phase", "open-document"},
                                 {"status", "failed"},
                                 {"error", std::string(reason)}}},
              connectionId);
    }
  }
  (void)result;
}

void WhiteboardApp::cancelPendingOpen() noexcept {
  if (!pendingOpen_) return;
  const auto requestId = pendingOpen_->requestId;
  const auto connectionId = pendingOpen_->connectionId;
  const bool admissionSent = openDocumentResponseSent_;
  pendingOpen_->callbackState->active = false;
  pendingOpen_->callbackState->owner = nullptr;
  stagingPendingOpen_ = false;
  pendingOpenNotificationFailure_.reset();
  (void)KillTimer(window_, embeddedCompletionTimeoutTimerId_);
  (void)embeddedLoadCompletions_.cancelGeneration(pendingOpen_->generation);
  (void)embeddedLoadTracker_.cancelGeneration(pendingOpen_->generation);
  pendingOpen_.reset();
  openDocumentResponsePending_ = false;
  openDocumentResponseSent_ = true;
  if (!closing_ && admissionSent && !requestId.empty()) {
    sendIpc(ipc::Message{
                1, "diagnostics", requestId,
                nlohmann::json{{"phase", "open-document"},
                               {"status", "superseded"}}},
            connectionId);
  }
}

void WhiteboardApp::sendIpc(
    const ipc::Message& message,
    NamedPipeServer::ConnectionId connectionId) {
  if (ipcServer_) ipcServer_->send(message, connectionId);
}

void WhiteboardApp::stopIpc() {
  canvas::app::stopAndClearWhiteboardIpc(*this);
}

std::uint32_t WhiteboardApp::captureMessageError() noexcept {
  return GetLastError();
}

bool WhiteboardApp::isWindowAlive() noexcept {
  return window_ != nullptr && IsWindow(window_) != FALSE;
}

void WhiteboardApp::destroyWindow() noexcept {
  if (window_ != nullptr) (void)DestroyWindow(window_);
}

void WhiteboardApp::stopAndJoinIpcServer() noexcept { ipcServer_.reset(); }

void WhiteboardApp::clearIpcCallbackQueue() noexcept {
  std::lock_guard<std::mutex> lock(ipcMessagesMutex_);
  ipcMessages_.clear();
  ipcQueuedBytes_ = 0;
  ipcMessagePosted_ = false;
  ipcQueueOverflowed_ = false;
}

void WhiteboardApp::clearWindowHandle() noexcept { window_ = nullptr; }

HRESULT WhiteboardApp::renderIpcDocument() {
  return renderDocument(document_);
}

HRESULT WhiteboardApp::renderDocument(const document::Document& source) {
  HRESULT result = baseLayer_.render(source, document::LayerClass::Base);
  if (SUCCEEDED(result)) {
    result = embeddedLayer_.render(source, document::LayerClass::Embedded);
  }
  if (SUCCEEDED(result)) {
    result = annotationLayer_.render(source, document::LayerClass::Annotation);
  }
  if (SUCCEEDED(result)) {
    result = chromeLayer_.render(source, document::LayerClass::Chrome);
  }
  return result;
}

void WhiteboardApp::handleIpcMessage(
    const ipc::Message& message,
    NamedPipeServer::ConnectionId connectionId) {
  if (message.type == "hello") {
    // start() occurs only after window creation, D3D12, the DComp tree, and
    // first render are complete. NamedPipeServer has authenticated hello
    // before it queues this message, making this the readiness boundary.
    sendIpc(ipc::Message{1, "ready", message.requestId,
                         nlohmann::json{{"protocolVersion", 1}}},
            connectionId);
    return;
  }

  bool accepted = true;
  std::string error;
  bool emitDocumentState = false;
  bool deferResponse = false;
  const auto stringField = [&message](const char* name)
      -> std::optional<std::string> {
    const auto value = message.payload.find(name);
    if (value == message.payload.end() || !value->is_string() ||
        value->get_ref<const std::string&>().empty()) {
      return std::nullopt;
    }
    return value->get<std::string>();
  };
  const auto setMode = [this](std::string_view mode) -> bool {
    if (mode == "draw") {
      inputRouter_.setMode(input::InputMode::Draw);
    } else if (mode == "select") {
      inputRouter_.setMode(input::InputMode::Select);
    } else if (mode == "interact") {
      inputRouter_.setMode(input::InputMode::Interact);
    } else {
      return false;
    }
    return true;
  };
  const auto documentMutationAllowed = [this, &error]() {
    if (pendingOpen_ != nullptr) {
      error = "document open in progress";
      return false;
    }
    return true;
  };

  try {
    if (message.type == "shutdown") {
      sendIpc(ipc::Message{1, "response", message.requestId,
                           nlohmann::json{{"accepted", true}}},
              connectionId);
      if (window_ != nullptr && IsWindow(window_)) {
        (void)PostMessageW(window_, WM_CLOSE, 0, 0);
      }
      return;
    }
    if (message.type == "set-tool") {
      const auto tool = stringField("tool");
      const auto fingerDraw = message.payload.find("fingerDrawEnabled");
      if (!tool) {
        error = "set-tool requires a tool";
      } else if (fingerDraw != message.payload.end() &&
                 !fingerDraw->is_boolean()) {
        error = "fingerDrawEnabled must be a boolean";
      } else if (!setMode(*tool)) {
        error = "unsupported tool";
      } else if (fingerDraw != message.payload.end()) {
        inputRouter_.setFingerDrawEnabled(fingerDraw->get<bool>());
      }
    } else if (message.type == "set-mode") {
      const auto mode = stringField("mode");
      if (!mode) error = "set-mode requires a mode";
      else if (!setMode(*mode)) error = "unsupported mode";
    } else if (message.type == "enter-interaction") {
      inputRouter_.setMode(input::InputMode::Interact);
    } else if (message.type == "leave-interaction") {
      inputRouter_.setMode(input::InputMode::Draw);
    } else if (message.type == "open-document") {
      const auto path = stringField("path");
      if (!path) error = "open-document requires a path";
      else if (const auto widePath = utf8ToWide(*path)) {
        const HRESULT result =
            openDocument(*widePath, message.requestId, connectionId);
        if (FAILED(result)) {
          error = FAILED(lastError_)
                      ? "could not open document; rollback failed and the "
                        "application is closing"
                      : "could not open document; previous state retained";
        } else {
          deferResponse = openDocumentResponsePending_ ||
                          openDocumentResponseSent_;
          emitDocumentState = !deferResponse;
        }
      } else error = "path is not valid UTF-8";
    } else if (message.type == "save-document") {
      if (documentMutationAllowed()) {
        const auto path = stringField("path");
        if (!path) error = "save-document requires a path";
        else if (const auto widePath = utf8ToWide(*path)) {
          if (FAILED(saveDocument(*widePath))) error = "could not save document";
        } else error = "path is not valid UTF-8";
      }
    } else if (message.type == "create-embedded") {
      if (!documentMutationAllowed()) {
        // Keep the command response as the single busy acknowledgement.
      } else {
      const auto kind = stringField("kind");
      document::EmbeddedKind embeddedKind = document::EmbeddedKind::Web;
      if (kind && *kind == "video") embeddedKind = document::EmbeddedKind::Video;
      else if (kind && (*kind == "rich-text" || *kind == "richtext")) {
        embeddedKind = document::EmbeddedKind::RichText;
      } else if (kind && *kind != "web") error = "unsupported embedded kind";
      if (error.empty()) {
        document::Node node;
        node.id = "embedded-" + message.requestId;
        node.layer = document::LayerClass::Embedded;
        node.bounds = core::Rect{100.0F, 100.0F, 360.0F, 240.0F};
        const auto source = stringField("source");
        const auto title = stringField("title");
        const std::string persistentSource = source.value_or(
            embeddedKind == document::EmbeddedKind::RichText
                ? "https://canvas.local/richtext.html"
                : (embeddedKind == document::EmbeddedKind::Video
                       ? "https://canvas.local/video.html"
                       : "https://example.com/"));
        node.payload = document::EmbeddedNode{embeddedKind, persistentSource,
                                               title.value_or("Electron object")};
        std::vector<HostedWebView> created;
        const HRESULT surfaceResult = createEmbeddedSurface(node, created, false);
        if (FAILED(surfaceResult)) {
          error = "could not create embedded surface";
        } else if (FAILED(setSurfaceVisible(*created.front().surface, true))) {
          error = "could not show embedded surface; no node was created";
        } else {
          const document::Document previous = document_;
          if (!document_.add(node)) {
            error = "could not create embedded node";
          } else {
            try {
              embeddedWebViews_.push_back(std::move(created.front()));
            } catch (...) {
              if (!document_.erase(node.id)) {
                reportFatalFailure(
                    E_UNEXPECTED, message.requestId,
                    "create-embedded allocation rollback failed", connectionId);
                error = "could not retain embedded surface; rollback failed "
                        "and the application is closing";
              } else {
                error = "could not retain embedded surface; no node was created";
              }
            }
            const HRESULT renderResult =
                error.empty() ? renderIpcDocument() : S_OK;
            if (FAILED(renderResult)) {
              const document::NodeId id = node.id;
              const bool removed = document_.erase(id);
              embeddedWebViews_.pop_back();
              const bool renderRestored = restorePreviousRender(
                  previous, message.requestId,
                  "create-embedded render rollback failed", connectionId);
              if (!removed || !renderRestored) {
                if (!removed) {
                  reportFatalFailure(
                      E_UNEXPECTED, message.requestId,
                      "create-embedded document rollback failed", connectionId);
                }
                error = "could not render document; rollback failed and the "
                        "application is closing";
              } else {
                error =
                    "could not render document; embedded node was rolled back";
              }
            }
            if (error.empty()) emitDocumentState = true;
          }
        }
      }
      }
    } else if (message.type == "set-embedded-bounds") {
      if (!documentMutationAllowed()) {
        // Keep the command response as the single busy acknowledgement.
      } else {
      const auto id = stringField("id");
      const auto x = message.payload.find("x");
      const auto y = message.payload.find("y");
      const auto width = message.payload.find("width");
      const auto height = message.payload.find("height");
      if (!id || x == message.payload.end() || y == message.payload.end() ||
          width == message.payload.end() || height == message.payload.end() ||
          !x->is_number() || !y->is_number() || !width->is_number() ||
          !height->is_number()) {
        error = "set-embedded-bounds requires id, x, y, width, and height";
      } else {
        const core::Rect bounds{x->get<float>(), y->get<float>(),
                                width->get<float>(), height->get<float>()};
        const document::Node* node = document_.find(*id);
        if (!isValidEmbeddedBounds(bounds) || node == nullptr ||
            node->layer != document::LayerClass::Embedded ||
            !std::holds_alternative<document::EmbeddedNode>(node->payload)) {
          error = "invalid embedded bounds or node id";
        } else {
          const core::Rect previousBounds = node->bounds;
          document::Document candidate = document_;
          WebView2Surface* surface = embeddedWebView(*id);
          if (surface == nullptr) {
            error = "embedded surface is unavailable";
          } else if (!candidate.setBounds(*id, bounds)) {
            error = "could not resize embedded node; bounds unchanged";
          } else {
            const HRESULT surfaceResult = setSurfaceBounds(*surface, bounds);
            if (FAILED(surfaceResult)) {
              const HRESULT rollbackResult =
                  setSurfaceBounds(*surface, previousBounds);
              if (FAILED(rollbackResult)) {
                reportFatalFailure(
                    rollbackResult, message.requestId,
                    "set-embedded-bounds surface rollback failed", connectionId);
                error = "could not resize embedded surface; rollback failed "
                        "and the application is closing";
              } else {
                error = "could not resize embedded surface; bounds unchanged";
              }
            } else {
              const HRESULT renderResult = renderDocument(candidate);
              if (FAILED(renderResult)) {
                const HRESULT surfaceRollback =
                    setSurfaceBounds(*surface, previousBounds);
                const bool renderRestored = restorePreviousRender(
                    document_, message.requestId,
                    "set-embedded-bounds render rollback failed", connectionId);
                if (FAILED(surfaceRollback) || !renderRestored) {
                  if (FAILED(surfaceRollback)) {
                    reportFatalFailure(
                        surfaceRollback, message.requestId,
                        "set-embedded-bounds surface rollback failed",
                        connectionId);
                  }
                  error = "could not render document; rollback failed and the "
                          "application is closing";
                } else {
                  error = "could not render document; previous bounds restored";
                }
              } else {
                document_ = std::move(candidate);
                emitDocumentState = true;
              }
            }
          }
        }
      }
      }
    } else if (message.type == "delete-node") {
      if (!documentMutationAllowed()) {
        // Keep the command response as the single busy acknowledgement.
      } else {
      const auto id = stringField("id");
      const document::Node* node = id ? document_.find(*id) : nullptr;
      if (node == nullptr) error = "unknown node id";
      else {
        document::Document candidate = document_;
        if (!candidate.erase(*id)) {
          error = "could not delete node";
        } else {
          const HRESULT renderResult = renderDocument(candidate);
          if (FAILED(renderResult)) {
            const bool renderRestored = restorePreviousRender(
                document_, message.requestId,
                "delete-node render rollback failed", connectionId);
            error = renderRestored
                        ? "could not render document; node was not deleted"
                        : "could not render document; rollback failed and the "
                          "application is closing";
          } else {
            document_ = std::move(candidate);
            embeddedWebViews_.erase(
                std::remove_if(
                    embeddedWebViews_.begin(), embeddedWebViews_.end(),
                    [this](const HostedWebView& view) {
                      const document::Node* retained =
                          document_.find(view.nodeId);
                      return retained == nullptr ||
                             retained->layer !=
                                 document::LayerClass::Embedded ||
                             !std::holds_alternative<document::EmbeddedNode>(
                                 retained->payload);
                    }),
                embeddedWebViews_.end());
            emitDocumentState = true;
          }
        }
      }
      }
    } else {
      accepted = false;
      error = "IPC message is not a launcher command";
    }
  } catch (const nlohmann::json::exception&) {
    error = "invalid command payload";
  }

  accepted = accepted && error.empty();
  nlohmann::json response{{"accepted", accepted}};
  if (!error.empty()) response["error"] = error;
  if (!deferResponse) {
    sendIpc(ipc::Message{1, "response", message.requestId, std::move(response)},
            connectionId);
  }
  if (!deferResponse && accepted && emitDocumentState) {
    sendIpc(ipc::Message{1, "document-state", "state-" + message.requestId,
                         nlohmann::json{{"nodeCount", document_.nodes().size()}}},
            connectionId);
  }
}

}  // namespace canvas::windows
