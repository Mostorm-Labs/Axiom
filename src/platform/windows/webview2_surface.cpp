#include "platform/windows/webview2_surface.h"

#include "platform/windows/dcomp_host.h"
#include "platform/windows/webview2_close_seam.h"
#include "platform/windows/webview2_virtual_host_path.h"

#include <WebView2.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace canvas::windows {

namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right) {
  if (left.size() != right.size()) return false;
  return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                              right.data(), static_cast<int>(right.size()),
                              TRUE) == CSTR_EQUAL;
}

bool startsWithIgnoreCase(std::wstring_view value,
                          std::wstring_view prefix) {
  return value.size() >= prefix.size() &&
         equalsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

std::optional<std::wstring> urlPart(const std::wstring& uri,
                                    URL_PART part) {
  wchar_t buffer[512]{};
  DWORD length = static_cast<DWORD>(std::size(buffer));
  const HRESULT hr = UrlGetPartW(uri.c_str(), buffer, &length, part, 0);
  if (FAILED(hr) || length == 0) return std::nullopt;
  return std::wstring(buffer, length);
}

// UrlGetPart performs structural parsing, so exact hostname comparisons do
// not accept values such as canvas.local.attacker.example or user-info spoofs.
WebView2Surface::NavigationClass classifyNavigationImpl(
    std::wstring_view candidate, bool allowTestDataUrls) {
  if (candidate.empty() ||
      candidate.find(L'\0') != std::wstring_view::npos) {
    return WebView2Surface::NavigationClass::Denied;
  }

  // Data navigation is retained solely behind the deterministic test option.
  if (allowTestDataUrls &&
      startsWithIgnoreCase(candidate, L"data:text/html,")) {
    return WebView2Surface::NavigationClass::TestData;
  }

  const std::wstring uri(candidate);
  const auto scheme = urlPart(uri, URL_PART_SCHEME);
  const auto host = urlPart(uri, URL_PART_HOSTNAME);
  if (!scheme || !host || host->empty() ||
      !equalsIgnoreCase(*scheme, L"https") ||
      UrlIsW(uri.c_str(), URLIS_URL) == FALSE) {
    return WebView2Surface::NavigationClass::Denied;
  }

  // Remote HTTPS content is allowed. The two local virtual hosts are matched
  // exactly by the same parsed hostname path (and never by string prefix).
  const bool isLocalVirtualHost =
      equalsIgnoreCase(*host, L"canvas.local") ||
      equalsIgnoreCase(*host, L"media.canvas.local");
  return isLocalVirtualHost
             ? WebView2Surface::NavigationClass::LocalVirtualHost
             : WebView2Surface::NavigationClass::Https;
}

bool isAllowedNavigationForOptions(
    std::wstring_view candidate, const WebView2Surface::Options& options) {
  const auto classification =
      classifyNavigationImpl(candidate, options.allowTestDataUrls);
  if (classification == WebView2Surface::NavigationClass::Denied) return false;
  if (classification != WebView2Surface::NavigationClass::LocalVirtualHost) {
    return true;
  }

  const auto host = urlPart(std::wstring(candidate), URL_PART_HOSTNAME);
  if (!host) return false;
  if (equalsIgnoreCase(*host, L"canvas.local")) {
    return options.canvasLocalFolder && !options.canvasLocalFolder->empty();
  }
  if (equalsIgnoreCase(*host, L"media.canvas.local")) {
    return options.mediaCanvasLocalFolder &&
           !options.mediaCanvasLocalFolder->empty();
  }
  return false;
}

HRESULT firstFailure(std::initializer_list<HRESULT> results) {
  for (const HRESULT result : results) {
    if (FAILED(result)) return result;
  }
  return S_OK;
}

std::optional<RECT> outwardRoundedRect(core::Rect bounds) {
  const double left = bounds.x;
  const double top = bounds.y;
  const double right = static_cast<double>(bounds.x) + bounds.width;
  const double bottom = static_cast<double>(bounds.y) + bounds.height;
  if (bounds.width < 0.0F || bounds.height < 0.0F || !std::isfinite(left) ||
      !std::isfinite(top) || !std::isfinite(right) ||
      !std::isfinite(bottom)) {
    return std::nullopt;
  }

  constexpr double kLongMin =
      static_cast<double>((std::numeric_limits<LONG>::min)());
  constexpr double kLongMax =
      static_cast<double>((std::numeric_limits<LONG>::max)());
  const double roundedLeft = std::floor(left);
  const double roundedTop = std::floor(top);
  const double roundedRight = std::ceil(right);
  const double roundedBottom = std::ceil(bottom);
  if (roundedLeft < kLongMin || roundedTop < kLongMin ||
      roundedRight > kLongMax || roundedBottom > kLongMax) {
    return std::nullopt;
  }
  return RECT{static_cast<LONG>(roundedLeft), static_cast<LONG>(roundedTop),
              static_cast<LONG>(roundedRight),
              static_cast<LONG>(roundedBottom)};
}

std::optional<COREWEBVIEW2_MOUSE_EVENT_KIND> mouseEventKind(UINT message) {
  switch (message) {
    case WM_MOUSEMOVE:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
    case WM_MOUSELEAVE:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE;
    case WM_LBUTTONDOWN:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
    case WM_LBUTTONUP:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
    case WM_LBUTTONDBLCLK:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK;
    case WM_RBUTTONDOWN:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN;
    case WM_RBUTTONUP:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;
    case WM_RBUTTONDBLCLK:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOUBLE_CLICK;
    case WM_MBUTTONDOWN:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN;
    case WM_MBUTTONUP:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP;
    case WM_MBUTTONDBLCLK:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOUBLE_CLICK;
    case WM_XBUTTONDOWN:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_DOWN;
    case WM_XBUTTONUP:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP;
    case WM_XBUTTONDBLCLK:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_DOUBLE_CLICK;
    case WM_MOUSEWHEEL:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL;
    case WM_MOUSEHWHEEL:
      return COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL;
    default:
      return std::nullopt;
  }
}

COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS webViewVirtualKeys(
    EmbeddedMouseButtons buttons) {
  UINT32 keys = COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
  const auto add = [&keys](COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS value) {
    keys |= static_cast<UINT32>(value);
  };
  if (hasEmbeddedMouseButton(buttons, EmbeddedMouseButton::Left)) {
    add(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON);
  }
  if (hasEmbeddedMouseButton(buttons, EmbeddedMouseButton::Right)) {
    add(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON);
  }
  if (hasEmbeddedMouseButton(buttons, EmbeddedMouseButton::Middle)) {
    add(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_MIDDLE_BUTTON);
  }
  if (hasEmbeddedMouseButton(buttons, EmbeddedMouseButton::X1)) {
    add(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_X_BUTTON1);
  }
  if (hasEmbeddedMouseButton(buttons, EmbeddedMouseButton::X2)) {
    add(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_X_BUTTON2);
  }
  if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
    add(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_SHIFT);
  }
  if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
    add(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_CONTROL);
  }
  return static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(keys);
}

std::optional<COREWEBVIEW2_POINTER_EVENT_KIND> pointerEventKind(UINT message) {
  switch (message) {
    case WM_POINTERDOWN:
      return COREWEBVIEW2_POINTER_EVENT_KIND_DOWN;
    case WM_POINTERUP:
      return COREWEBVIEW2_POINTER_EVENT_KIND_UP;
    case WM_POINTERUPDATE:
      return COREWEBVIEW2_POINTER_EVENT_KIND_UPDATE;
    case WM_POINTERENTER:
      return COREWEBVIEW2_POINTER_EVENT_KIND_ENTER;
    case WM_POINTERLEAVE:
    case WM_POINTERCAPTURECHANGED:
      return COREWEBVIEW2_POINTER_EVENT_KIND_LEAVE;
    default:
      return std::nullopt;
  }
}

POINT pixelToHimetric(POINT pixel, RECT deviceRect, RECT displayRect) {
  const auto displayWidth = displayRect.right - displayRect.left;
  const auto displayHeight = displayRect.bottom - displayRect.top;
  const auto deviceWidth = deviceRect.right - deviceRect.left;
  const auto deviceHeight = deviceRect.bottom - deviceRect.top;
  if (displayWidth == 0 || displayHeight == 0) return {};
  return POINT{
      deviceRect.left + MulDiv(pixel.x - displayRect.left, deviceWidth,
                               displayWidth),
      deviceRect.top + MulDiv(pixel.y - displayRect.top, deviceHeight,
                              displayHeight)};
}

}  // namespace

struct WebView2Surface::Impl final
    : std::enable_shared_from_this<WebView2Surface::Impl>,
      detail::WebView2CloseOperations,
      EmbeddedMouseCancellationSink {
  Impl(DCompHost& compositionHost, HWND window, Options surfaceOptions)
      : host(&compositionHost),
        hostWindow(window),
        ownerThread(GetCurrentThreadId()),
        options(std::move(surfaceOptions)) {}

  ~Impl() {
    // WebView2 controllers are apartment-bound. Cross-thread destruction is a
    // contract violation, not a reason to silently release STA COM pointers.
    if (state != State::Closed) std::terminate();
  }

  HRESULT initialize() {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (state != State::Created) return E_UNEXPECTED;
    if (host == nullptr || hostWindow == nullptr || !IsWindow(hostWindow)) {
      return fail(E_INVALIDARG);
    }

    APTTYPE apartmentType{};
    APTTYPEQUALIFIER qualifier{};
    const HRESULT apartmentResult =
        CoGetApartmentType(&apartmentType, &qualifier);
    if (FAILED(apartmentResult)) return fail(apartmentResult);
    if (apartmentType != APTTYPE_STA && apartmentType != APTTYPE_MAINSTA) {
      return fail(RPC_E_WRONG_THREAD);
    }

    HRESULT hr = host->createChildVisual(
        VisualSlot::EmbeddedContent, rootVisual.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return fail(hr);
    compositionParent = host->visual(VisualSlot::EmbeddedContent);
    compositionDevice = host->device();
    host = nullptr;
    if (!compositionParent || !compositionDevice) return fail(E_UNEXPECTED);
    state = State::Initializing;
    lastResult = S_OK;

    const std::weak_ptr<Impl> weak = weak_from_this();
    const auto completed =
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak](HRESULT result,
                   ICoreWebView2Environment* createdEnvironment) -> HRESULT {
              const auto self = weak.lock();
              if (!self) return S_OK;
              self->onEnvironmentCreated(result, createdEnvironment);
              return S_OK;
            });
    if (!completed) return fail(E_OUTOFMEMORY);
    hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
                                                  completed.Get());
    return FAILED(hr) ? fail(hr) : S_OK;
  }

  HRESULT navigate(std::wstring_view requestedUri) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (!isAllowedNavigationForOptions(requestedUri, options)) {
      return E_ACCESSDENIED;
    }
    if (state == State::Closed) return RO_E_CLOSED;
    if (state == State::Failed) return lastResult;

    pendingNavigation.assign(requestedUri);
    if (!webView) return S_OK;
    const HRESULT hr = webView->Navigate(pendingNavigation->c_str());
    if (FAILED(hr)) return fail(hr);
    pendingNavigation.reset();
    return S_OK;
  }

  HRESULT setBounds(core::Rect requestedBounds) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    const auto rounded = outwardRoundedRect(requestedBounds);
    if (!rounded) return remember(E_INVALIDARG);
    bounds = *rounded;
    if (!controller) return S_OK;
    const HRESULT hr = controller->put_Bounds(bounds);
    if (FAILED(hr)) return remember(hr);
    return remember(controller->NotifyParentWindowPositionChanged());
  }

  HRESULT setVisible(bool requestedVisible) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    visible = requestedVisible;
    if (!controller) return S_OK;
    return remember(controller->put_IsVisible(visible ? TRUE : FALSE));
  }

  HRESULT close() {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (state == State::Closed) return closeResult;

    // Every operation is attempted even after a failure. Local ownership is
    // then released unconditionally so an explicit close cannot leave a
    // half-closed apartment-bound object behind.
    closeResult = detail::runWebView2CloseOperations(*this);
    releaseOwnership();
    state = State::Closed;
    if (FAILED(closeResult)) lastResult = closeResult;
    return closeResult;
  }

  void setInteractive(bool requestedInteractive) {
    requireOwnerThread();
    interactive = requestedInteractive;
  }

  void requireOwnerThread() const {
    if (FAILED(checkThread())) std::terminate();
  }

  HRESULT forwardMouseMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (!interactive || !visible) return S_FALSE;
    if (state == State::Closed) return RO_E_CLOSED;
    if (state != State::Ready || !compositionController) return E_PENDING;

    const auto kind = mouseEventKind(message);
    if (!kind) return E_INVALIDARG;
    if (message == WM_MOUSELEAVE) {
      return compositionController->SendMouseInput(
          *kind, COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, POINT{});
    }
    const auto keys = static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(
        LOWORD(wParam));
    UINT32 mouseData = 0;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
      mouseData = static_cast<UINT32>(GET_WHEEL_DELTA_WPARAM(wParam));
    } else if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ||
               message == WM_XBUTTONDBLCLK) {
      mouseData = static_cast<UINT32>(GET_XBUTTON_WPARAM(wParam));
    }
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
      if (!ScreenToClient(hostWindow, &point)) {
        return HRESULT_FROM_WIN32(GetLastError());
      }
    }
    point.x -= bounds.left;
    point.y -= bounds.top;
    return compositionController->SendMouseInput(*kind, keys, mouseData,
                                                 point);
  }

  HRESULT cancelMouseButtons(EmbeddedMouseButtons buttons) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (state == State::Closed) return RO_E_CLOSED;
    if (state != State::Ready || !compositionController) return E_PENDING;
    return runEmbeddedMouseCancellation(buttons, *this);
  }

  HRESULT sendLeave() noexcept override {
    return compositionController
               ? compositionController->SendMouseInput(
                     COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE,
                     COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, POINT{})
               : E_PENDING;
  }

  HRESULT sendButtonUp(EmbeddedMouseButton button,
                       EmbeddedMouseButtons remainingButtons)
      noexcept override {
    if (!compositionController) return E_PENDING;
    COREWEBVIEW2_MOUSE_EVENT_KIND kind{};
    UINT32 mouseData = 0;
    switch (button) {
      case EmbeddedMouseButton::Left:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
        break;
      case EmbeddedMouseButton::Right:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;
        break;
      case EmbeddedMouseButton::Middle:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP;
        break;
      case EmbeddedMouseButton::X1:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP;
        mouseData = XBUTTON1;
        break;
      case EmbeddedMouseButton::X2:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP;
        mouseData = XBUTTON2;
        break;
      default:
        return E_INVALIDARG;
    }
    constexpr POINT kOutsideSurface{-1, -1};
    return compositionController->SendMouseInput(
        kind, webViewVirtualKeys(remainingButtons), mouseData,
        kOutsideSurface);
  }

  HRESULT forwardTouchMessage(UINT message, UINT32 pointerId) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (!interactive || !visible) return S_FALSE;
    if (state == State::Closed) return RO_E_CLOSED;
    if (state != State::Ready || !environment3 || !compositionController) {
      return E_PENDING;
    }
    const auto kind = pointerEventKind(message);
    if (!kind) return E_INVALIDARG;

    POINTER_TOUCH_INFO touch{};
    if (!GetPointerTouchInfo(pointerId, &touch)) {
      return HRESULT_FROM_WIN32(GetLastError());
    }

    ComPtr<ICoreWebView2PointerInfo> pointer;
    HRESULT hr =
        environment3->CreateCoreWebView2PointerInfo(pointer.GetAddressOf());
    if (FAILED(hr)) return hr;

    POINTER_INFO& info = touch.pointerInfo;
    RECT deviceRect{};
    RECT displayRect{};
    const bool hasDeviceRects =
        GetPointerDeviceRects(info.sourceDevice, &deviceRect, &displayRect) !=
        FALSE;
    POINT clientPixel = info.ptPixelLocation;
    POINT clientPixelRaw = info.ptPixelLocationRaw;
    if (!ScreenToClient(hostWindow, &clientPixel) ||
        !ScreenToClient(hostWindow, &clientPixelRaw)) {
      return HRESULT_FROM_WIN32(GetLastError());
    }
    clientPixel.x -= bounds.left;
    clientPixel.y -= bounds.top;
    clientPixelRaw.x -= bounds.left;
    clientPixelRaw.y -= bounds.top;
    RECT clientContact = touch.rcContact;
    RECT clientContactRaw = touch.rcContactRaw;
    POINT* contactPoints = reinterpret_cast<POINT*>(&clientContact);
    POINT* rawContactPoints = reinterpret_cast<POINT*>(&clientContactRaw);
    if (!ScreenToClient(hostWindow, &contactPoints[0]) ||
        !ScreenToClient(hostWindow, &contactPoints[1]) ||
        !ScreenToClient(hostWindow, &rawContactPoints[0]) ||
        !ScreenToClient(hostWindow, &rawContactPoints[1])) {
      return HRESULT_FROM_WIN32(GetLastError());
    }
    OffsetRect(&clientContact, -bounds.left, -bounds.top);
    OffsetRect(&clientContactRaw, -bounds.left, -bounds.top);

    hr = firstFailure({
        pointer->put_PointerKind(static_cast<DWORD>(PT_TOUCH)),
        pointer->put_PointerId(info.pointerId),
        pointer->put_FrameId(info.frameId),
        pointer->put_PointerFlags(static_cast<UINT32>(info.pointerFlags)),
        pointer->put_PixelLocation(clientPixel),
        pointer->put_PixelLocationRaw(clientPixelRaw),
        pointer->put_Time(info.dwTime),
        pointer->put_HistoryCount(info.historyCount),
        pointer->put_InputData(info.InputData),
        pointer->put_KeyStates(info.dwKeyStates),
        pointer->put_PerformanceCount(info.PerformanceCount),
        pointer->put_ButtonChangeKind(static_cast<INT32>(info.ButtonChangeType)),
        pointer->put_TouchFlags(static_cast<UINT32>(touch.touchFlags)),
        pointer->put_TouchMask(static_cast<UINT32>(touch.touchMask)),
        pointer->put_TouchContact(clientContact),
        pointer->put_TouchContactRaw(clientContactRaw),
        pointer->put_TouchOrientation(touch.orientation),
        pointer->put_TouchPressure(touch.pressure),
    });
    if (FAILED(hr)) return hr;

    if (hasDeviceRects) {
      const POINT himetric =
          pixelToHimetric(info.ptPixelLocation, deviceRect, displayRect);
      const POINT himetricRaw =
          pixelToHimetric(info.ptPixelLocationRaw, deviceRect, displayRect);
      hr = firstFailure({pointer->put_PointerDeviceRect(deviceRect),
                         pointer->put_DisplayRect(displayRect),
                         pointer->put_HimetricLocation(himetric),
                         pointer->put_HimetricLocationRaw(himetricRaw)});
      if (FAILED(hr)) return hr;
    }

    return compositionController->SendPointerInput(*kind, pointer.Get());
  }

  HRESULT onEnvironmentCreated(HRESULT result,
                               ICoreWebView2Environment* createdEnvironment) {
    if (FAILED(checkThread()) || state != State::Initializing) return S_FALSE;
    if (FAILED(result)) return fail(result);
    if (createdEnvironment == nullptr) return fail(E_POINTER);

    environment = createdEnvironment;
    HRESULT hr = environment.As(&environment3);
    if (FAILED(hr)) return fail(hr);

    const std::weak_ptr<Impl> weak = weak_from_this();
    const auto completed = Callback<
        ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
        [weak](HRESULT controllerResult,
               ICoreWebView2CompositionController* createdController)
            -> HRESULT {
          const auto self = weak.lock();
          if (!self) return S_OK;
          self->onControllerCreated(controllerResult, createdController);
          return S_OK;
        });
    if (!completed) return fail(E_OUTOFMEMORY);
    hr = environment3->CreateCoreWebView2CompositionController(
        hostWindow, completed.Get());
    return FAILED(hr) ? fail(hr) : S_OK;
  }

  HRESULT onControllerCreated(
      HRESULT result, ICoreWebView2CompositionController* createdController) {
    if (FAILED(checkThread()) || state != State::Initializing) return S_FALSE;
    if (FAILED(result)) return fail(result);
    if (createdController == nullptr) return fail(E_POINTER);

    compositionController = createdController;
    HRESULT hr = compositionController.As(&controller);
    if (FAILED(hr)) return fail(hr);
    hr = compositionController->put_RootVisualTarget(rootVisual.Get());
    if (FAILED(hr)) return fail(hr);
    hr = controller->get_CoreWebView2(webView.GetAddressOf());
    if (FAILED(hr) || !webView) return fail(FAILED(hr) ? hr : E_POINTER);
    hr = configureSecurity();
    if (FAILED(hr)) return fail(hr);
    hr = configureVirtualHosts();
    if (FAILED(hr)) return fail(hr);
    hr = registerEventHandlers();
    if (FAILED(hr)) return fail(hr);
    hr = controller->put_Bounds(bounds);
    if (SUCCEEDED(hr)) {
      hr = controller->NotifyParentWindowPositionChanged();
    }
    if (SUCCEEDED(hr)) {
      hr = controller->put_IsVisible(visible ? TRUE : FALSE);
    }
    if (FAILED(hr)) return fail(hr);

    state = State::Ready;
    lastResult = S_OK;
    if (pendingNavigation) {
      const std::wstring uri = std::move(*pendingNavigation);
      pendingNavigation.reset();
      hr = webView->Navigate(uri.c_str());
      if (FAILED(hr)) return fail(hr);
    }
    return S_OK;
  }

  HRESULT configureSecurity() {
    ComPtr<ICoreWebView2Settings> settings;
    HRESULT hr = webView->get_Settings(settings.GetAddressOf());
    if (FAILED(hr) || !settings) return FAILED(hr) ? hr : E_POINTER;
    return firstFailure({
        settings->put_AreDefaultScriptDialogsEnabled(FALSE),
        settings->put_AreDevToolsEnabled(FALSE),
        settings->put_IsStatusBarEnabled(FALSE),
        settings->put_IsZoomControlEnabled(FALSE),
        settings->put_AreHostObjectsAllowed(FALSE),
    });
  }

  HRESULT configureVirtualHosts() {
    if (!options.canvasLocalFolder && !options.mediaCanvasLocalFolder) {
      return S_OK;
    }

    HRESULT hr = webView.As(&webView3);
    if (FAILED(hr) || !webView3) return FAILED(hr) ? hr : E_NOINTERFACE;
    std::wstring executableFolder;
    hr = detail::executableDirectory(executableFolder);
    if (FAILED(hr)) return hr;

    const auto mapFolder = [this, &executableFolder](
                               LPCWSTR hostName,
                               const std::optional<std::wstring>& folder,
                               bool& mapped) -> HRESULT {
      if (!folder) return S_OK;
      std::wstring normalizedFolder;
      HRESULT result = detail::normalizeVirtualHostFolder(
          *folder, executableFolder, normalizedFolder);
      if (FAILED(result)) return result;

      // Validation and WebView2 mapping intentionally consume the identical
      // normalized absolute string; process CWD never participates.
      const DWORD attributes = GetFileAttributesW(normalizedFolder.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES) {
        return detail::win32FailureOr(
            HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND));
      }
      if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
      }
      result = webView3->SetVirtualHostNameToFolderMapping(
          hostName, normalizedFolder.c_str(),
          COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
      if (SUCCEEDED(result)) mapped = true;
      return result;
    };

    hr = mapFolder(L"canvas.local", options.canvasLocalFolder,
                   mappedCanvasLocal);
    if (FAILED(hr)) return hr;
    return mapFolder(L"media.canvas.local", options.mediaCanvasLocalFolder,
                     mappedMediaCanvasLocal);
  }

  HRESULT registerEventHandlers() {
    const std::weak_ptr<Impl> weak = weak_from_this();
    auto navigation = Callback<ICoreWebView2NavigationStartingEventHandler>(
        [weak](ICoreWebView2*,
               ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
          const auto self = weak.lock();
          if (!self || args == nullptr) return S_OK;
          LPWSTR rawUri = nullptr;
          const HRESULT uriResult = args->get_Uri(&rawUri);
          const bool allowed =
              SUCCEEDED(uriResult) && rawUri != nullptr &&
              isAllowedNavigationForOptions(rawUri, self->options);
          CoTaskMemFree(rawUri);
          return allowed ? S_OK : args->put_Cancel(TRUE);
        });
    if (!navigation) return E_OUTOFMEMORY;
    HRESULT hr =
        webView->add_NavigationStarting(navigation.Get(), &navigationToken);
    if (FAILED(hr)) return hr;
    hasNavigationToken = true;

    auto newWindow = Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [weak](ICoreWebView2*,
               ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
          if (weak.expired() || args == nullptr) return S_OK;
          return args->put_Handled(TRUE);
        });
    if (!newWindow) return E_OUTOFMEMORY;
    hr = webView->add_NewWindowRequested(newWindow.Get(), &newWindowToken);
    if (FAILED(hr)) return hr;
    hasNewWindowToken = true;

    auto permission = Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [weak](ICoreWebView2*,
               ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
          if (weak.expired() || args == nullptr) return S_OK;
          return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
        });
    if (!permission) return E_OUTOFMEMORY;
    hr = webView->add_PermissionRequested(permission.Get(), &permissionToken);
    if (FAILED(hr)) return hr;
    hasPermissionToken = true;

    auto webMessage = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [weak](ICoreWebView2*,
               ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
          const auto self = weak.lock();
          if (!self || args == nullptr) return S_OK;
          LPWSTR value = nullptr;
          HRESULT messageResult = args->TryGetWebMessageAsString(&value);
          if (SUCCEEDED(messageResult) && value != nullptr) {
            self->capturedMessages.emplace_back(value);
            CoTaskMemFree(value);
            return S_OK;
          }
          CoTaskMemFree(value);
          value = nullptr;
          messageResult = args->get_WebMessageAsJson(&value);
          if (SUCCEEDED(messageResult) && value != nullptr) {
            self->capturedMessages.emplace_back(value);
          }
          CoTaskMemFree(value);
          return S_OK;
        });
    if (!webMessage) return E_OUTOFMEMORY;
    hr = webView->add_WebMessageReceived(webMessage.Get(), &webMessageToken);
    if (FAILED(hr)) return hr;
    hasWebMessageToken = true;
    return S_OK;
  }

  HRESULT checkThread() const {
    return GetCurrentThreadId() == ownerThread ? S_OK : RPC_E_WRONG_THREAD;
  }

  HRESULT remember(HRESULT result) {
    if (FAILED(result)) lastResult = result;
    return result;
  }

  HRESULT fail(HRESULT result) {
    lastResult = FAILED(result) ? result : E_FAIL;
    state = State::Failed;
    return lastResult;
  }

  HRESULT removeEventHandlers() noexcept override {
    HRESULT firstResult = S_OK;
    const auto record = [&firstResult](HRESULT result) {
      if (SUCCEEDED(firstResult) && FAILED(result)) firstResult = result;
    };
    if (hasWebMessageToken) {
      if (webView) {
        record(webView->remove_WebMessageReceived(webMessageToken));
      }
      hasWebMessageToken = false;
    }
    if (hasPermissionToken) {
      if (webView) {
        record(webView->remove_PermissionRequested(permissionToken));
      }
      hasPermissionToken = false;
    }
    if (hasNewWindowToken) {
      if (webView) {
        record(webView->remove_NewWindowRequested(newWindowToken));
      }
      hasNewWindowToken = false;
    }
    if (hasNavigationToken) {
      if (webView) {
        record(webView->remove_NavigationStarting(navigationToken));
      }
      hasNavigationToken = false;
    }
    return firstResult;
  }

  HRESULT clearVirtualHostMappings() noexcept override {
    HRESULT firstResult = S_OK;
    if (webView3 && mappedCanvasLocal) {
      firstResult = webView3->ClearVirtualHostNameToFolderMapping(
          L"canvas.local");
    }
    mappedCanvasLocal = false;
    if (webView3 && mappedMediaCanvasLocal) {
      const HRESULT result = webView3->ClearVirtualHostNameToFolderMapping(
          L"media.canvas.local");
      if (SUCCEEDED(firstResult) && FAILED(result)) firstResult = result;
    }
    mappedMediaCanvasLocal = false;
    return firstResult;
  }

  HRESULT detachRootVisualTarget() noexcept override {
    return compositionController
               ? compositionController->put_RootVisualTarget(nullptr)
               : S_OK;
  }

  HRESULT hideController() noexcept override {
    return controller ? controller->put_IsVisible(FALSE) : S_OK;
  }

  HRESULT closeController() noexcept override {
    return controller ? controller->Close() : S_OK;
  }

  HRESULT removeChildVisual() noexcept override {
    return compositionParent && rootVisual
               ? compositionParent->RemoveVisual(rootVisual.Get())
               : S_OK;
  }

  HRESULT commitComposition() noexcept override {
    return compositionDevice ? compositionDevice->Commit() : S_OK;
  }

  void releaseOwnership() noexcept {
    webView.Reset();
    webView3.Reset();
    controller.Reset();
    compositionController.Reset();
    environment3.Reset();
    environment.Reset();
    rootVisual.Reset();
    compositionParent.Reset();
    compositionDevice.Reset();
  }

  DCompHost* host = nullptr;
  HWND hostWindow = nullptr;
  DWORD ownerThread = 0;
  State state = State::Created;
  HRESULT lastResult = S_OK;
  HRESULT closeResult = S_OK;
  Options options;
  RECT bounds{0, 0, 1, 1};
  bool visible = true;
  bool interactive = false;
  std::optional<std::wstring> pendingNavigation;
  std::vector<std::wstring> capturedMessages;

  ComPtr<IDCompositionVisual> rootVisual;
  ComPtr<IDCompositionVisual> compositionParent;
  ComPtr<IDCompositionDevice> compositionDevice;
  ComPtr<ICoreWebView2Environment> environment;
  ComPtr<ICoreWebView2Environment3> environment3;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2CompositionController> compositionController;
  ComPtr<ICoreWebView2> webView;
  ComPtr<ICoreWebView2_3> webView3;

  EventRegistrationToken navigationToken{};
  EventRegistrationToken newWindowToken{};
  EventRegistrationToken permissionToken{};
  EventRegistrationToken webMessageToken{};
  bool hasNavigationToken = false;
  bool hasNewWindowToken = false;
  bool hasPermissionToken = false;
  bool hasWebMessageToken = false;
  bool mappedCanvasLocal = false;
  bool mappedMediaCanvasLocal = false;
};

WebView2Surface::WebView2Surface(DCompHost& host, HWND hostWindow)
    : WebView2Surface(host, hostWindow, Options{}) {}

WebView2Surface::WebView2Surface(DCompHost& host, HWND hostWindow,
                                 Options options)
    : impl_(std::make_shared<Impl>(host, hostWindow, std::move(options))) {}

WebView2Surface::~WebView2Surface() {
  // Cleanup failures cannot be reported from a destructor and all ownership
  // has already been released. Only violating the creating-STA contract is
  // fatal because releasing apartment-bound COM pointers there is unsafe.
  if (FAILED(impl_->checkThread())) std::terminate();
  (void)impl_->close();
}

HRESULT WebView2Surface::initialize() { return impl_->initialize(); }

HRESULT WebView2Surface::close() { return impl_->close(); }

HRESULT WebView2Surface::navigate(std::wstring_view uri) {
  return impl_->navigate(uri);
}

HRESULT WebView2Surface::forwardMouseMessage(UINT message, WPARAM wParam,
                                             LPARAM lParam) {
  return impl_->forwardMouseMessage(message, wParam, lParam);
}

HRESULT WebView2Surface::cancelMouseButtons(EmbeddedMouseButtons buttons) {
  return impl_->cancelMouseButtons(buttons);
}

HRESULT WebView2Surface::forwardTouchMessage(UINT message, UINT32 pointerId) {
  return impl_->forwardTouchMessage(message, pointerId);
}

void WebView2Surface::setBounds(core::Rect bounds) {
  impl_->requireOwnerThread();
  impl_->setBounds(bounds);
}

void WebView2Surface::setInteractive(bool interactive) {
  impl_->setInteractive(interactive);
}

void WebView2Surface::setVisible(bool visible) {
  impl_->requireOwnerThread();
  impl_->setVisible(visible);
}

WebView2Surface::State WebView2Surface::state() const noexcept {
  impl_->requireOwnerThread();
  return impl_->state;
}

HRESULT WebView2Surface::lastResult() const noexcept {
  impl_->requireOwnerThread();
  return impl_->lastResult;
}

bool WebView2Surface::interactive() const noexcept {
  impl_->requireOwnerThread();
  return impl_->interactive;
}

const std::vector<std::wstring>& WebView2Surface::capturedMessages() const
    noexcept {
  impl_->requireOwnerThread();
  return impl_->capturedMessages;
}

WebView2Surface::NavigationClass WebView2Surface::classifyNavigation(
    std::wstring_view uri, bool allowTestDataUrls) {
  return classifyNavigationImpl(uri, allowTestDataUrls);
}

}  // namespace canvas::windows
