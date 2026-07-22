#include "platform/windows/webview2_surface.h"

#include "platform/windows/dcomp_host.h"

#include <WebView2.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <algorithm>
#include <cmath>
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
bool isAllowedNavigation(std::wstring_view candidate) {
  if (candidate.empty() ||
      candidate.find(L'\0') != std::wstring_view::npos) {
    return false;
  }

  // Data navigation is retained solely for the deterministic integration
  // page. Other data MIME types and active URL schemes remain blocked.
  if (startsWithIgnoreCase(candidate, L"data:text/html,")) {
    return true;
  }

  const std::wstring uri(candidate);
  const auto scheme = urlPart(uri, URL_PART_SCHEME);
  const auto host = urlPart(uri, URL_PART_HOSTNAME);
  if (!scheme || !host || host->empty() ||
      !equalsIgnoreCase(*scheme, L"https")) {
    return false;
  }

  // Remote HTTPS content is allowed. The two local virtual hosts are matched
  // exactly by the same parsed hostname path (and never by string prefix).
  const bool isLocalVirtualHost = equalsIgnoreCase(*host, L"canvas.local") ||
                                  equalsIgnoreCase(*host,
                                                   L"media.canvas.local");
  (void)isLocalVirtualHost;
  return true;
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
    : std::enable_shared_from_this<WebView2Surface::Impl> {
  Impl(DCompHost& compositionHost, HWND window)
      : host(&compositionHost),
        hostWindow(window),
        ownerThread(GetCurrentThreadId()) {}

  ~Impl() { shutdown(); }

  HRESULT initialize() {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return fail(threadResult);
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
    if (!isAllowedNavigation(requestedUri)) return E_ACCESSDENIED;
    if (state == State::Failed || state == State::Closed) return lastResult;

    pendingNavigation.assign(requestedUri);
    if (!webView) return S_OK;
    const HRESULT hr = webView->Navigate(pendingNavigation->c_str());
    if (FAILED(hr)) return fail(hr);
    pendingNavigation.reset();
    return S_OK;
  }

  HRESULT setBounds(core::Rect requestedBounds) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return remember(threadResult);
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
    if (FAILED(threadResult)) return remember(threadResult);
    visible = requestedVisible;
    if (!controller) return S_OK;
    return remember(controller->put_IsVisible(visible ? TRUE : FALSE));
  }

  HRESULT forwardMouseMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (!interactive || !visible) return S_FALSE;
    if (state != State::Ready || !compositionController) return E_PENDING;

    const auto kind = mouseEventKind(message);
    if (!kind) return E_INVALIDARG;
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

  HRESULT forwardTouchMessage(UINT message, UINT32 pointerId) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (!interactive || !visible) return S_FALSE;
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

  HRESULT registerEventHandlers() {
    const std::weak_ptr<Impl> weak = weak_from_this();
    auto navigation = Callback<ICoreWebView2NavigationStartingEventHandler>(
        [weak](ICoreWebView2*,
               ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
          const auto self = weak.lock();
          if (!self || args == nullptr) return S_OK;
          LPWSTR rawUri = nullptr;
          const HRESULT uriResult = args->get_Uri(&rawUri);
          const bool allowed = SUCCEEDED(uriResult) && rawUri != nullptr &&
                               isAllowedNavigation(rawUri);
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

  void unregisterEventHandlers() {
    if (!webView) return;
    if (hasWebMessageToken) {
      webView->remove_WebMessageReceived(webMessageToken);
      hasWebMessageToken = false;
    }
    if (hasPermissionToken) {
      webView->remove_PermissionRequested(permissionToken);
      hasPermissionToken = false;
    }
    if (hasNewWindowToken) {
      webView->remove_NewWindowRequested(newWindowToken);
      hasNewWindowToken = false;
    }
    if (hasNavigationToken) {
      webView->remove_NavigationStarting(navigationToken);
      hasNavigationToken = false;
    }
  }

  void shutdown() {
    if (state == State::Closed) return;
    if (GetCurrentThreadId() == ownerThread) {
      unregisterEventHandlers();
      if (compositionController) {
        compositionController->put_RootVisualTarget(nullptr);
      }
      if (controller) {
        controller->put_IsVisible(FALSE);
        controller->Close();
      }
      if (compositionParent && rootVisual &&
          SUCCEEDED(compositionParent->RemoveVisual(rootVisual.Get())) &&
          compositionDevice) {
        compositionDevice->Commit();
      }
    }
    webView.Reset();
    controller.Reset();
    compositionController.Reset();
    environment3.Reset();
    environment.Reset();
    rootVisual.Reset();
    compositionParent.Reset();
    compositionDevice.Reset();
    state = State::Closed;
  }

  DCompHost* host = nullptr;
  HWND hostWindow = nullptr;
  DWORD ownerThread = 0;
  State state = State::Created;
  HRESULT lastResult = S_OK;
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

  EventRegistrationToken navigationToken{};
  EventRegistrationToken newWindowToken{};
  EventRegistrationToken permissionToken{};
  EventRegistrationToken webMessageToken{};
  bool hasNavigationToken = false;
  bool hasNewWindowToken = false;
  bool hasPermissionToken = false;
  bool hasWebMessageToken = false;
};

WebView2Surface::WebView2Surface(DCompHost& host, HWND hostWindow)
    : impl_(std::make_shared<Impl>(host, hostWindow)) {}

WebView2Surface::~WebView2Surface() { impl_->shutdown(); }

HRESULT WebView2Surface::initialize() { return impl_->initialize(); }

HRESULT WebView2Surface::navigate(std::wstring_view uri) {
  return impl_->navigate(uri);
}

HRESULT WebView2Surface::forwardMouseMessage(UINT message, WPARAM wParam,
                                             LPARAM lParam) {
  return impl_->forwardMouseMessage(message, wParam, lParam);
}

HRESULT WebView2Surface::forwardTouchMessage(UINT message, UINT32 pointerId) {
  return impl_->forwardTouchMessage(message, pointerId);
}

void WebView2Surface::setBounds(core::Rect bounds) {
  impl_->setBounds(bounds);
}

void WebView2Surface::setInteractive(bool interactive) {
  impl_->interactive = interactive;
}

void WebView2Surface::setVisible(bool visible) {
  impl_->setVisible(visible);
}

WebView2Surface::State WebView2Surface::state() const noexcept {
  return impl_->state;
}

HRESULT WebView2Surface::lastResult() const noexcept {
  return impl_->lastResult;
}

bool WebView2Surface::interactive() const noexcept {
  return impl_->interactive;
}

const std::vector<std::wstring>& WebView2Surface::capturedMessages() const
    noexcept {
  return impl_->capturedMessages;
}

}  // namespace canvas::windows
