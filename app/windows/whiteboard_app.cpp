#include "whiteboard_app.h"

#include "canvas/document/embedded_transform.h"
#include "canvas/storage/document_codec.h"
#include "platform/windows/document_store.h"
#include "platform/windows/webview2_media_source.h"
#include "platform/windows/win_pointer_adapter.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
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

int hresultExitCode(HRESULT hr) {
  return FAILED(hr) ? static_cast<int>(hr) : 1;
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
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
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
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
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

std::wstring jsonEscape(std::wstring_view value) {
  std::wstring result;
  result.reserve(value.size() + 8U);
  constexpr wchar_t kHex[] = L"0123456789ABCDEF";
  for (const wchar_t character : value) {
    switch (character) {
      case L'"':
        result += L"\\\"";
        break;
      case L'\\':
        result += L"\\\\";
        break;
      case L'\b':
        result += L"\\b";
        break;
      case L'\f':
        result += L"\\f";
        break;
      case L'\n':
        result += L"\\n";
        break;
      case L'\r':
        result += L"\\r";
        break;
      case L'\t':
        result += L"\\t";
        break;
      default:
        if (character < 0x20) {
          const auto value16 = static_cast<unsigned int>(character);
          result += L"\\u00";
          result.push_back(kHex[(value16 >> 4U) & 0x0FU]);
          result.push_back(kHex[value16 & 0x0FU]);
        } else {
          result.push_back(character);
        }
        break;
    }
  }
  return result;
}

}  // namespace

int WhiteboardApp::run(HINSTANCE instance, int commandShow,
                       const WhiteboardRunOptions& options) {
  lastError_ = S_OK;
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
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
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
  const bool diagnosticLayers = options.selfTestLayers ||
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
    const bool populated = options.openPath
                               ? true
                               : (options.selfTestEmbedded ||
                                          options.selfTestDocument
                                      ? populateEmbeddedSelfTestDocument()
                                      : populateSelfTestDocument());
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
      surface->setBounds(node->bounds);
      surface->setInteractive(false);
      surface->setVisible(true);
      HRESULT result = surface->initialize();
      if (SUCCEEDED(result)) result = surface->navigate(uri);
      if (SUCCEEDED(result) && initialMessage) {
        result = surface->postMessage(*initialMessage);
      }
      if (FAILED(result)) return result;
      embeddedWebViews_.push_back(
          HostedWebView{nodeId, std::move(surface)});
      return S_OK;
    };

    if (options.selfTestEmbedded && !options.openPath) {
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
            std::wstring(
                L"{\"protocolVersion\":1,\"type\":\"set-video-source\","
                L"\"nodeId\":\"video-1\",\"payload\":{\"source\":\"") +
            mediaSource.uri + L"\"}}";
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
        const auto source = utf8ToWide(embedded->source);
        const auto wideNodeId = utf8ToWide(node.id);
        const auto encodedNodeId =
            wideNodeId ? percentEncodeQueryComponent(*wideNodeId) :
                         std::optional<std::wstring>{};
        if (!source || !wideNodeId || !encodedNodeId || source->empty() ||
            wideNodeId->empty() || wideNodeId->size() > 256U) {
          layerResult = E_INVALIDARG;
          break;
        }
        WebView2Surface::Options surfaceOptions;
        std::wstring uri = *source;
        std::optional<std::wstring> initialMessage;
        const bool packagedSource =
            _wcsnicmp(uri.c_str(), L"https://canvas.local/", 21) == 0;
        if (packagedSource) surfaceOptions.canvasLocalFolder = L"web";

        if (packagedSource &&
            embedded->kind == document::EmbeddedKind::RichText) {
          uri = L"https://canvas.local/richtext.html?nodeId=" +
                *encodedNodeId;
        } else if (packagedSource &&
                   embedded->kind == document::EmbeddedKind::Video) {
          uri = L"https://canvas.local/video.html?nodeId=" +
                *encodedNodeId;
        }

        if (embedded->kind == document::EmbeddedKind::Video &&
            options.videoPath) {
          detail::LocalMediaSource mediaSource;
          layerResult =
              detail::approveLocalMediaFile(*options.videoPath, mediaSource);
          if (FAILED(layerResult)) break;
          surfaceOptions.canvasLocalFolder = L"web";
          surfaceOptions.mediaCanvasLocalFolder = mediaSource.folder;
          uri = L"https://canvas.local/video.html?nodeId=" +
                *encodedNodeId;
          initialMessage =
              std::wstring(
                  L"{\"protocolVersion\":1,\"type\":\"set-video-source\","
                  L"\"nodeId\":\"") +
              jsonEscape(*wideNodeId) + L"\",\"payload\":{\"source\":\"" +
              jsonEscape(mediaSource.uri) + L"\"}}";
        } else if (embedded->kind == document::EmbeddedKind::Video &&
                   uri.rfind(L"https://", 0) != 0) {
          detail::LocalMediaSource mediaSource;
          layerResult = detail::approveLocalMediaFile(uri, mediaSource);
          if (FAILED(layerResult)) break;
          surfaceOptions.canvasLocalFolder = L"web";
          surfaceOptions.mediaCanvasLocalFolder = mediaSource.folder;
          uri = L"https://canvas.local/video.html?nodeId=" +
                *encodedNodeId;
          initialMessage =
              std::wstring(
                  L"{\"protocolVersion\":1,\"type\":\"set-video-source\","
                  L"\"nodeId\":\"") +
              jsonEscape(*wideNodeId) + L"\",\"payload\":{\"source\":\"" +
              jsonEscape(mediaSource.uri) + L"\"}}";
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

  UnregisterClassW(kWindowClassName, instance);
  if (FAILED(lastError_)) return hresultExitCode(lastError_);
  return messageResult < 0 ? hresultExitCode(HRESULT_FROM_WIN32(GetLastError()))
                           : static_cast<int>(message.wParam);
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
      (void)app->forwardMouseToEmbedded(window, WM_CANCELMODE, 0, 0);
      app->embeddedWebViews_.clear();
    }
    PostQuitMessage(0);
    return 0;
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
      document::EmbeddedKind::Web, "about:blank", "Embedded placeholder"};
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

HRESULT WhiteboardApp::openDocument(const std::wstring& path) {
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
  if (!storage::DocumentCodec::decodeInto(bytes, document_, decodeError)) {
    if (!decodeError.empty()) {
      OutputDebugStringA(("Canvas document decode failed: " + decodeError +
                          "\n")
                             .c_str());
    }
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
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

}  // namespace canvas::windows
