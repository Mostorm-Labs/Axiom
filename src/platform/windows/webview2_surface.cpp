#include "platform/windows/webview2_surface.h"

#include "platform/windows/dcomp_host.h"
#include "platform/windows/webview2_close_seam.h"
#include "platform/windows/webview2_initial_load_seam.h"
#include "platform/windows/webview2_message_log.h"
#include "platform/windows/webview2_navigation_uri.h"
#include "platform/windows/webview2_virtual_host_path.h"

#include <WebView2.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// WebView2 returns caller-owned NUL-terminated strings. Never construct a
// view with an unbounded wcslen: a malformed provider or hostile page must be
// rejected after at most the same bounded scan used by navigation policy.
std::optional<std::wstring_view> boundedWideView(
    const wchar_t* value, std::size_t maximumCodeUnits) noexcept {
  if (value == nullptr) return std::nullopt;
  for (std::size_t length = 0U; length <= maximumCodeUnits; ++length) {
    if (value[length] == L'\0') return std::wstring_view(value, length);
  }
  return std::nullopt;
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
      candidate.find(L'\0') != std::wstring_view::npos ||
      candidate.size() > detail::kWebView2MaxNavigationCodeUnits) {
    return WebView2Surface::NavigationClass::Denied;
  }

  // Data navigation is retained solely behind the deterministic test option.
  if (allowTestDataUrls &&
      startsWithIgnoreCase(candidate, L"data:text/html,")) {
    return WebView2Surface::NavigationClass::TestData;
  }

  try {
    const std::wstring uri(candidate);
    const auto scheme = urlPart(uri, URL_PART_SCHEME);
    const auto host = urlPart(uri, URL_PART_HOSTNAME);
    if (!scheme || !host || host->empty() ||
        !equalsIgnoreCase(*scheme, L"https") ||
        UrlIsW(uri.c_str(), URLIS_URL) == FALSE) {
      return WebView2Surface::NavigationClass::Denied;
    }

    // Remote HTTPS content is allowed. The two local virtual hosts are
    // matched exactly by the same parsed hostname path (and never by string
    // prefix).
    const bool isLocalVirtualHost =
        equalsIgnoreCase(*host, L"canvas.local") ||
        equalsIgnoreCase(*host, L"media.canvas.local");
    return isLocalVirtualHost
               ? WebView2Surface::NavigationClass::LocalVirtualHost
               : WebView2Surface::NavigationClass::Https;
  } catch (...) {
    return WebView2Surface::NavigationClass::Denied;
  }
}

bool isAllowedNavigationForOptions(
    std::wstring_view candidate, const WebView2Surface::Options& options) {
  const auto classification =
      classifyNavigationImpl(candidate, options.allowTestDataUrls);
  if (classification == WebView2Surface::NavigationClass::Denied) return false;
  if (classification != WebView2Surface::NavigationClass::LocalVirtualHost) {
    return true;
  }

  try {
    const auto host = urlPart(std::wstring(candidate), URL_PART_HOSTNAME);
    if (!host) return false;
    if (equalsIgnoreCase(*host, L"canvas.local")) {
      return options.canvasLocalFolder && !options.canvasLocalFolder->empty();
    }
    if (equalsIgnoreCase(*host, L"media.canvas.local")) {
      return options.mediaCanvasLocalFolder &&
             !options.mediaCanvasLocalFolder->empty();
    }
  } catch (...) {
    return false;
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
  using NavigationGeneration =
      detail::NavigationRequestGenerationTracker::Generation;

  struct PendingNavigationRequest {
    std::wstring uri;
    std::wstring documentKey;
    NavigationGeneration generation = 0U;
    bool hasFragment = false;
    bool documentKeyReady = false;
  };

  enum class NativeNavigationPhase { Preparing, Calling, AwaitingStart };

  struct IssuedNavigation {
    PendingNavigationRequest request;
    NativeNavigationPhase phase = NativeNavigationPhase::Preparing;
    bool expectsNavigationStarting = true;
    std::uint64_t lifetimeEpoch = 0U;
  };

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
    // The factory may invoke completed synchronously, and that callback may
    // notify client code which destroys the owning WebView2Surface. Retain the
    // PImpl until the factory returns and all state checks below are finished.
    const auto keepAlive = shared_from_this();
    (void)keepAlive;
    hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
                                                  completed.Get());
    // The factory is allowed to invoke its completion before returning. Do
    // not overwrite a state transition performed by that callback (including
    // a re-entrant close or failure) with the outer call's HRESULT.
    if (state == State::Closed) return RO_E_CLOSED;
    if (state == State::Failed) return lastResult;
    if (state == State::Ready) return S_OK;
    if (state != State::Initializing) return S_FALSE;
    return FAILED(hr) ? fail(hr) : S_OK;
  }

  HRESULT navigate(std::wstring_view requestedUri) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    initialLoad.noteNavigationRequest();
    if (requestedUri.size() > detail::kWebView2MaxNavigationCodeUnits) {
      return E_INVALIDARG;
    }
    if (!isAllowedNavigationForOptions(requestedUri, options)) {
      return E_ACCESSDENIED;
    }
    if (state == State::Closed) return RO_E_CLOSED;
    if (state == State::Failed) return lastResult;

    PendingNavigationRequest request;
    try {
      request.uri.assign(requestedUri);
    } catch (...) {
      return remember(E_OUTOFMEMORY);
    }

    request.hasFragment = detail::navigationUriHasFragment(request.uri);

    // Queueing is deliberately a non-COM operation.  The public surface is
    // allowed to receive its first data navigation before WebView2 startup
    // (and the close/cancellation contract is tested before CoInitializeEx),
    // so URLMon must not be consulted until a real WebView is ready.  The
    // identity is completed by navigatePrepared() immediately before the
    // native Navigate call.
    if (detail::navigationUriHasScheme(request.uri, L"data")) {
      // The opt-in test scheme has a COM-free identity path.  Besides avoiding
      // needless work, this keeps a queued request useful if startup later
      // fails before a WebView object is created.
      const HRESULT copyResult =
          detail::copyOpaqueDocumentUri(request.uri, request.documentKey);
      if (FAILED(copyResult)) return copyResult;
      request.documentKeyReady = true;
    }
    const auto generation = navigationRequestGenerations.begin();
    if (!generation) return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    request.generation = *generation;

    // WebView can become non-null while controller setup is still making COM
    // calls. Re-entrant navigation during that interval must remain queued
    // until every security, event, bounds, and visibility step has succeeded.
    if (state != State::Ready || !webView) {
      try {
        pendingNavigation.emplace(std::move(request));
      } catch (...) {
        return remember(E_OUTOFMEMORY);
      }
      (void)initialLoad.request();
      pendingHostMessages.clear();
      navigationComplete = false;
      return S_OK;
    }
    return navigatePrepared(std::move(request));
  }

  bool issuedNavigationMatches(
      ICoreWebView2* capturedWebView,
      NavigationGeneration generation) const noexcept {
    return !closeInProgress && state == State::Ready &&
           capturedWebView != nullptr && webView.Get() == capturedWebView &&
           issuedNavigation &&
           issuedNavigation->request.generation == generation;
  }

  bool issuedNavigationOperationIsCurrent(
      ICoreWebView2* capturedWebView,
      NavigationGeneration generation,
      std::uint64_t lifetimeEpoch) const noexcept {
    return issuedNavigationMatches(capturedWebView, generation) &&
           issuedNavigation->lifetimeEpoch == lifetimeEpoch &&
           navigationMutationEpoch == lifetimeEpoch;
  }

  bool restartIssuedNavigationAfterMutation(
      ICoreWebView2* capturedWebView,
      NavigationGeneration generation) noexcept {
    if (!issuedNavigationMatches(capturedWebView, generation) ||
        issuedNavigation->phase != NativeNavigationPhase::Preparing) {
      return false;
    }
    issuedNavigation->lifetimeEpoch = navigationMutationEpoch;
    return true;
  }

  bool hasNewerDeferredNavigation(
      NavigationGeneration generation) const noexcept {
    return deferredNavigation &&
           deferredNavigation->generation > generation;
  }

  void clearNavigationDriveState() noexcept {
    issuedNavigation.reset();
    deferredNavigation.reset();
    navigationDriveRequested = false;
  }

  HRESULT deferNavigation(PendingNavigationRequest request) {
    try {
      deferredNavigation.emplace(std::move(request));
    } catch (...) {
      return E_OUTOFMEMORY;
    }
    // Queue replacement is itself a navigation-state mutation. In particular,
    // a WebView2 event-argument getter can synchronously re-enter navigate()
    // while the driver is active; advancing here makes the outer callback
    // stale even when the completed initial load leaves its active id intact.
    advanceNavigationMutationEpoch();
    (void)initialLoad.request();
    pendingHostMessages.clear();
    // Once the one-shot initial load is still pending, any valid replacement
    // supersedes the active document immediately. Retiring its id before the
    // next URLMon/WebView2 call prevents a re-entrant completion for the old
    // page from terminating the initial-load promise while the replacement is
    // being prepared. A completed document is retained until preflight proves
    // whether the replacement is a same-document fragment navigation.
    if (initialLoad.state() == detail::InitialLoadState::Pending &&
        activeNavigationId != 0U) {
      activeNavigationId = 0U;
      activeNavigationSourceCommitted = false;
      activeNavigationDocumentKey.clear();
      navigationComplete = false;
      initialLoad.retireActiveNavigation();
      advanceActiveNavigationRevision();
    }
    return S_OK;
  }

  void advanceNavigationMutationEpoch() noexcept {
    ++navigationMutationEpoch;
    if (navigationMutationEpoch == 0U) ++navigationMutationEpoch;
  }

  void advanceActiveNavigationRevision() noexcept {
    ++activeNavigationRevision;
    if (activeNavigationRevision == 0U) ++activeNavigationRevision;
    advanceNavigationMutationEpoch();
  }

  bool navigationMutationIsCurrent(
      ICoreWebView2* capturedWebView,
      std::uint64_t mutationEpoch) const noexcept {
    return !closeInProgress && state == State::Ready &&
           capturedWebView != nullptr && webView.Get() == capturedWebView &&
           navigationMutationEpoch == mutationEpoch;
  }

  bool sourceChangeIsCurrent(ICoreWebView2* capturedWebView,
                             UINT64 navigationId,
                             std::uint64_t navigationRevision) const noexcept {
    return !closeInProgress && state == State::Ready &&
           capturedWebView != nullptr &&
           webView.Get() == capturedWebView && navigationId != 0U &&
           activeNavigationId == navigationId &&
           activeNavigationRevision == navigationRevision;
  }

  HRESULT nativeNavigationExpectsStarting(
      ICoreWebView2* capturedWebView,
      const PendingNavigationRequest& request,
      std::uint64_t lifetimeEpoch,
      bool& expectsNavigationStarting) {
    expectsNavigationStarting = true;
    const NavigationGeneration generation = request.generation;
    const bool requestedHasFragment = request.hasFragment;
    std::wstring requestedDocumentKey;
    try {
      requestedDocumentKey = request.documentKey;
    } catch (...) {
      return E_OUTOFMEMORY;
    }
    // An earlier full-document Navigate is still waiting for its start, so
    // get_Source can only describe the document it is replacing. Do not use
    // that stale source to classify a later request as same-document.
    if (initialLoad.pendingNativeNavigationStarts() != 0U ||
        initialLoad.state() == detail::InitialLoadState::Pending) {
      return S_OK;
    }
    if (!navigationComplete && !activeNavigationSourceCommitted) return S_OK;

    LPWSTR rawSource = nullptr;
    const HRESULT sourceResult = capturedWebView->get_Source(&rawSource);
    if (!issuedNavigationOperationIsCurrent(capturedWebView, generation,
                                            lifetimeEpoch) ||
        hasNewerDeferredNavigation(generation)) {
      CoTaskMemFree(rawSource);
      return S_FALSE;
    }
    if (FAILED(sourceResult) || rawSource == nullptr) {
      CoTaskMemFree(rawSource);
      return FAILED(sourceResult) ? sourceResult : E_POINTER;
    }
    const auto sourceView = boundedWideView(
        rawSource, detail::kWebView2MaxNavigationCodeUnits);
    if (!sourceView) {
      CoTaskMemFree(rawSource);
      return E_INVALIDARG;
    }

    std::wstring sourceDocumentKey;
    const bool sourceHasFragment = detail::navigationUriHasFragment(*sourceView);
    const HRESULT canonicalResult = detail::canonicalDocumentUri(
        *sourceView, sourceDocumentKey);
    CoTaskMemFree(rawSource);
    if (FAILED(canonicalResult)) return canonicalResult;

    if (!issuedNavigationOperationIsCurrent(capturedWebView, generation,
                                            lifetimeEpoch) ||
        hasNewerDeferredNavigation(generation)) {
      return S_FALSE;
    }
    expectsNavigationStarting = detail::nativeNavigationExpectsStarting(
        true, false, {sourceDocumentKey, sourceHasFragment},
        {requestedDocumentKey, requestedHasFragment});
    return S_OK;
  }

  HRESULT navigatePrepared(PendingNavigationRequest request) {
    const HRESULT deferResult = deferNavigation(std::move(request));
    if (FAILED(deferResult)) return deferResult;
    return driveNavigation();
  }

  HRESULT driveNavigation() {
    if (navigationDriveActive) {
      navigationDriveRequested = true;
      return S_OK;
    }
    if (navigationStartDispatchBlocked) {
      navigationDriveRequested = true;
      return S_OK;
    }

    const auto keepAlive = shared_from_this();
    (void)keepAlive;
    navigationDriveActive = true;
    HRESULT finalResult = S_OK;

    for (;;) {
      navigationDriveRequested = false;
      if (closeInProgress || state == State::Closed) {
        finalResult = RO_E_CLOSED;
        break;
      }
      if (state == State::Failed) {
        finalResult = FAILED(lastResult) ? lastResult : E_FAIL;
        break;
      }
      if (state != State::Ready || !webView) break;
      if (issuedNavigation &&
          issuedNavigation->phase == NativeNavigationPhase::AwaitingStart) {
        break;
      }

      if (!issuedNavigation) {
        if (!deferredNavigation) break;
        IssuedNavigation issued;
        issued.request = std::move(*deferredNavigation);
        deferredNavigation.reset();
        advanceNavigationMutationEpoch();
        issued.lifetimeEpoch = navigationMutationEpoch;
        issuedNavigation.emplace(std::move(issued));
      }

      const NavigationGeneration generation =
          issuedNavigation->request.generation;
      std::uint64_t lifetimeEpoch = issuedNavigation->lifetimeEpoch;
      const ComPtr<ICoreWebView2> capturedWebView = webView;

      if (!issuedNavigation->request.documentKeyReady) {
        std::wstring documentKey;
        const HRESULT canonicalResult = detail::canonicalDocumentUri(
            issuedNavigation->request.uri, documentKey);
        if (!issuedNavigationOperationIsCurrent(
                capturedWebView.Get(), generation, lifetimeEpoch)) {
          (void)restartIssuedNavigationAfterMutation(
              capturedWebView.Get(), generation);
          continue;
        }
        if (hasNewerDeferredNavigation(generation)) {
          issuedNavigation.reset();
          continue;
        }
        if (FAILED(canonicalResult)) {
          issuedNavigation.reset();
          finalResult = failInitialNavigation(canonicalResult);
          break;
        }
        issuedNavigation->request.documentKey.swap(documentKey);
        issuedNavigation->request.documentKeyReady = true;
      }

      bool expectsNavigationStarting = true;
      const HRESULT preflightResult = nativeNavigationExpectsStarting(
          capturedWebView.Get(), issuedNavigation->request, lifetimeEpoch,
          expectsNavigationStarting);
      if (!issuedNavigationOperationIsCurrent(
              capturedWebView.Get(), generation, lifetimeEpoch)) {
        (void)restartIssuedNavigationAfterMutation(
            capturedWebView.Get(), generation);
        continue;
      }
      if (hasNewerDeferredNavigation(generation)) {
        issuedNavigation.reset();
        continue;
      }
      if (preflightResult != S_OK) {
        issuedNavigation.reset();
        if (preflightResult == S_FALSE) continue;
        finalResult = failInitialNavigation(preflightResult);
        break;
      }

      std::wstring nativeUri;
      try {
        nativeUri = issuedNavigation->request.uri;
      } catch (...) {
        issuedNavigation.reset();
        if (deferredNavigation) continue;
        finalResult = failInitialNavigation(E_OUTOFMEMORY);
        break;
      }

      issuedNavigation->expectsNavigationStarting =
          expectsNavigationStarting;
      if (!initialLoad.tryBeginNativeNavigationForRequest(
              generation, expectsNavigationStarting)) {
        issuedNavigation.reset();
        finalResult = failInitialNavigation(E_UNEXPECTED);
        break;
      }
      (void)initialLoad.request();
      if (expectsNavigationStarting) {
        pendingHostMessages.clear();
        navigationComplete = false;
        activeNavigationSourceCommitted = false;
        activeNavigationDocumentKey.clear();
        activeNavigationId = 0U;
        advanceActiveNavigationRevision();
        issuedNavigation->lifetimeEpoch = navigationMutationEpoch;
        lifetimeEpoch = navigationMutationEpoch;
      }
      issuedNavigation->phase = NativeNavigationPhase::Calling;

      HRESULT navigateResult = S_OK;
      try {
        navigateResult = capturedWebView->Navigate(nativeUri.c_str());
      } catch (...) {
        navigateResult = E_OUTOFMEMORY;
      }

      const bool sameIssuedNavigation =
          issuedNavigationMatches(capturedWebView.Get(), generation);
      if (!sameIssuedNavigation && state == State::Closed) {
        finalResult = RO_E_CLOSED;
        break;
      }
      if (!sameIssuedNavigation && state == State::Failed) {
        finalResult = FAILED(lastResult) ? lastResult : E_FAIL;
        break;
      }
      if (sameIssuedNavigation && FAILED(navigateResult)) {
        (void)initialLoad.cancelPendingNativeNavigation(generation);
        issuedNavigation.reset();
        if (deferredNavigation) continue;
        finalResult = failInitialNavigation(navigateResult);
        break;
      }
      if (sameIssuedNavigation) {
        if (expectsNavigationStarting) {
          issuedNavigation->phase = NativeNavigationPhase::AwaitingStart;
          break;
        }
        issuedNavigation.reset();
        const HRESULT flushResult =
            flushPendingHostMessagesForCurrentNavigation();
        if (flushResult == S_FALSE) {
          if (state == State::Closed) {
            finalResult = RO_E_CLOSED;
            break;
          }
          if (state == State::Failed) {
            finalResult = FAILED(lastResult) ? lastResult : E_FAIL;
            break;
          }
          continue;
        }
        if (FAILED(flushResult)) {
          finalResult = flushResult;
          break;
        }
      } else if (FAILED(navigateResult) && !deferredNavigation &&
                 state == State::Ready) {
        finalResult = failInitialNavigation(navigateResult);
        break;
      }

      if (!deferredNavigation && !navigationDriveRequested) break;
    }

    navigationDriveActive = false;
    return finalResult;
  }

  HRESULT setInitialLoadCompletionHandler(
      InitialLoadCompletionHandler handler) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (state == State::Closed) return RO_E_CLOSED;
    if (!handler) return E_INVALIDARG;

    try {
      auto bridge = [handler = std::move(handler)](
                        detail::InitialLoadCompletion completion) mutable {
        handler({toPublicInitialLoadState(completion.state),
                 static_cast<HRESULT>(completion.result)});
      };
      return initialLoad.setCompletionHandler(std::move(bridge)) ? S_OK
                                                                  : E_UNEXPECTED;
    } catch (...) {
      return E_OUTOFMEMORY;
    }
  }

  HRESULT postMessage(std::wstring_view message) {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (message.empty() || message.find(L'\0') != std::wstring_view::npos) {
      return E_INVALIDARG;
    }
    if (message.size() > detail::kWebView2MaxMessageCodeUnits) {
      return E_INVALIDARG;
    }
    if (state == State::Closed) return RO_E_CLOSED;
    if (state == State::Failed) return lastResult;
    if (!webView || pendingNavigation || issuedNavigation ||
        deferredNavigation || !navigationComplete) {
      const auto result = pendingHostMessages.tryPush(message);
      if (result == detail::MessagePushResult::Oversized) {
        return E_INVALIDARG;
      }
      return result == detail::MessagePushResult::AllocationFailure
                 ? E_OUTOFMEMORY
                 : S_OK;
    }
    try {
      std::wstring owned(message);
      return remember(webView->PostWebMessageAsString(owned.c_str()));
    } catch (...) {
      return remember(E_OUTOFMEMORY);
    }
  }

  HRESULT focus() {
    const HRESULT threadResult = checkThread();
    if (FAILED(threadResult)) return threadResult;
    if (!interactive || !visible) return S_FALSE;
    if (state == State::Closed) return RO_E_CLOSED;
    if (state != State::Ready || !controller) return E_PENDING;
    return remember(
        controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC));
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
    if (closeInProgress) return S_FALSE;

    const auto keepAlive = shared_from_this();
    (void)keepAlive;
    closeInProgress = true;

    // Every operation is attempted even after a failure. Local ownership is
    // then released unconditionally so an explicit close cannot leave a
    // half-closed apartment-bound object behind.
    navigationRequestGenerations.invalidate();
    advanceActiveNavigationRevision();
    initialLoad.cancel();
    clearNavigationDriveState();
    closeResult = detail::runWebView2CloseOperations(*this);
    releaseOwnership();
    state = State::Closed;
    closeInProgress = false;
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

    const auto keepAlive = shared_from_this();
    (void)keepAlive;
    ComPtr<ICoreWebView2Environment> capturedEnvironment(createdEnvironment);
    ComPtr<ICoreWebView2Environment3> capturedEnvironment3;
    HRESULT hr = capturedEnvironment.As(&capturedEnvironment3);
    if (closeInProgress || state != State::Initializing) return S_FALSE;
    if (FAILED(hr) || !capturedEnvironment3) {
      return fail(FAILED(hr) ? hr : E_POINTER);
    }
    environment = capturedEnvironment;
    environment3 = capturedEnvironment3;

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
    hr = capturedEnvironment3->CreateCoreWebView2CompositionController(
        hostWindow, completed.Get());
    if (state == State::Closed) return RO_E_CLOSED;
    if (state == State::Failed) return lastResult;
    if (state == State::Ready) return S_OK;
    if (state != State::Initializing) return S_FALSE;
    return FAILED(hr) ? fail(hr) : S_OK;
  }

  HRESULT onControllerCreated(
      HRESULT result, ICoreWebView2CompositionController* createdController) {
    if (FAILED(checkThread()) || state != State::Initializing) return S_FALSE;
    if (FAILED(result)) return fail(result);
    if (createdController == nullptr) return fail(E_POINTER);

    const auto keepAlive = shared_from_this();
    (void)keepAlive;
    const auto setupIsCurrent =
        [this](ICoreWebView2CompositionController* capturedComposition,
               ICoreWebView2Controller* capturedController,
               ICoreWebView2* capturedWebView) noexcept {
          return !closeInProgress && state == State::Initializing &&
                 compositionController.Get() == capturedComposition &&
                 controller.Get() == capturedController &&
                 webView.Get() == capturedWebView;
        };

    ComPtr<ICoreWebView2CompositionController> capturedComposition(
        createdController);
    ComPtr<ICoreWebView2Controller> capturedController;
    HRESULT hr = capturedComposition.As(&capturedController);
    if (closeInProgress || state != State::Initializing) return S_FALSE;
    if (FAILED(hr) || !capturedController) return fail(FAILED(hr) ? hr : E_POINTER);

    ComPtr<IDCompositionVisual> capturedRootVisual = rootVisual;
    hr = capturedComposition->put_RootVisualTarget(capturedRootVisual.Get());
    if (closeInProgress || state != State::Initializing) {
      (void)capturedComposition->put_RootVisualTarget(nullptr);
      return S_FALSE;
    }
    if (FAILED(hr)) return fail(hr);

    ComPtr<ICoreWebView2> capturedWebView;
    hr = capturedController->get_CoreWebView2(
        capturedWebView.GetAddressOf());
    if (closeInProgress || state != State::Initializing) {
      (void)capturedComposition->put_RootVisualTarget(nullptr);
      return S_FALSE;
    }
    if (FAILED(hr) || !capturedWebView) {
      (void)capturedComposition->put_RootVisualTarget(nullptr);
      return fail(FAILED(hr) ? hr : E_POINTER);
    }

    compositionController = capturedComposition;
    controller = capturedController;
    webView = capturedWebView;
    if (!setupIsCurrent(capturedComposition.Get(), capturedController.Get(),
                        capturedWebView.Get())) {
      return S_FALSE;
    }

    hr = configureSecurity(capturedWebView.Get());
    if (!setupIsCurrent(capturedComposition.Get(), capturedController.Get(),
                        capturedWebView.Get())) {
      return S_FALSE;
    }
    if (FAILED(hr)) return fail(hr);
    if (hr != S_OK) return fail(E_UNEXPECTED);

    ComPtr<ICoreWebView2_3> capturedWebView3;
    bool capturedCanvasMapped = false;
    bool capturedMediaMapped = false;
    hr = configureVirtualHosts(capturedWebView.Get(), capturedWebView3,
                               capturedCanvasMapped, capturedMediaMapped);
    if (!setupIsCurrent(capturedComposition.Get(), capturedController.Get(),
                        capturedWebView.Get())) {
      return S_FALSE;
    }
    if (capturedCanvasMapped || capturedMediaMapped) {
      webView3 = capturedWebView3;
      mappedCanvasLocal = capturedCanvasMapped;
      mappedMediaCanvasLocal = capturedMediaMapped;
    }
    if (FAILED(hr)) return fail(hr);
    if (hr != S_OK) return fail(E_UNEXPECTED);
    webView3 = capturedWebView3;
    mappedCanvasLocal = capturedCanvasMapped;
    mappedMediaCanvasLocal = capturedMediaMapped;

    hr = registerEventHandlers(capturedWebView.Get());
    if (!setupIsCurrent(capturedComposition.Get(), capturedController.Get(),
                        capturedWebView.Get())) {
      return S_FALSE;
    }
    if (FAILED(hr)) return fail(hr);
    if (hr != S_OK) return fail(E_UNEXPECTED);

    hr = capturedController->put_Bounds(bounds);
    if (closeInProgress || state != State::Initializing) return S_FALSE;
    if (SUCCEEDED(hr)) {
      hr = capturedController->NotifyParentWindowPositionChanged();
      if (closeInProgress || state != State::Initializing) return S_FALSE;
    }
    if (SUCCEEDED(hr)) {
      hr = capturedController->put_IsVisible(visible ? TRUE : FALSE);
      if (closeInProgress || state != State::Initializing) return S_FALSE;
    }
    if (FAILED(hr)) return fail(hr);
    if (hr != S_OK) return fail(E_UNEXPECTED);

    state = State::Ready;
    lastResult = S_OK;
    if (pendingNavigation) {
      PendingNavigationRequest request = std::move(*pendingNavigation);
      pendingNavigation.reset();
      hr = navigatePrepared(std::move(request));
      if (FAILED(hr)) {
        // Startup has already admitted the controller, so a preflight or
        // synchronous Navigate failure must not strand InitialLoadTracker in
        // Pending.  navigatePrepared() owns failures reported after entering
        // WebView2 and may already have transitioned the surface; only fill
        // the gap when the surface is still otherwise usable.
        if (state == State::Ready &&
            initialLoad.state() == detail::InitialLoadState::Pending) {
          return failInitialNavigation(hr);
        }
        return hr;
      }
    }
    return S_OK;
  }

  HRESULT flushPendingHostMessagesForCurrentNavigation() {
    if (deferredNavigation || issuedNavigation) return S_FALSE;
    if (!navigationComplete || activeNavigationId == 0U || !webView) {
      return S_OK;
    }
    const ComPtr<ICoreWebView2> capturedWebView = webView;
    const UINT64 capturedNavigationId = activeNavigationId;
    const std::uint64_t capturedNavigationRevision =
        activeNavigationRevision;
    advanceNavigationMutationEpoch();
    const std::uint64_t mutationEpoch = navigationMutationEpoch;
    auto messages = pendingHostMessages.takeValues();
    for (const auto& message : messages) {
      const HRESULT result =
          capturedWebView->PostWebMessageAsString(message.c_str());
      if (!navigationMutationIsCurrent(capturedWebView.Get(), mutationEpoch) ||
          activeNavigationId != capturedNavigationId ||
          activeNavigationRevision != capturedNavigationRevision) {
        return S_FALSE;
      }
      if (FAILED(result)) return failInitialNavigation(result);
    }
    return S_OK;
  }

  HRESULT onNavigationCompleted(
      ICoreWebView2* sender,
      ICoreWebView2NavigationCompletedEventArgs* args) {
    if (FAILED(checkThread()) || closeInProgress || state != State::Ready ||
        sender == nullptr || args == nullptr || webView.Get() != sender) {
      return S_FALSE;
    }
    const auto keepAlive = shared_from_this();
    (void)keepAlive;
    const ComPtr<ICoreWebView2> capturedWebView(sender);
    const std::uint64_t callbackMutationEpoch = navigationMutationEpoch;
    // There is intentionally no active id while a replacement Navigate() is
    // synchronously retiring its predecessor. Ignore completions in that gap
    // before even consulting their result; they cannot belong to the new page.
    if (activeNavigationId == 0 || issuedNavigation || deferredNavigation) {
      return S_OK;
    }
    const UINT64 capturedActiveNavigationId = activeNavigationId;
    const std::uint64_t capturedNavigationRevision =
        activeNavigationRevision;
    UINT64 navigationId = 0;
    HRESULT successResult = args->get_NavigationId(&navigationId);
    if (!navigationMutationIsCurrent(capturedWebView.Get(),
                                     callbackMutationEpoch) ||
        activeNavigationId != capturedActiveNavigationId ||
        activeNavigationRevision != capturedNavigationRevision) {
      return S_OK;
    }
    if (FAILED(successResult)) return failInitialNavigation(successResult);
    if (navigationId != capturedActiveNavigationId) {
      return S_OK;
    }
    BOOL succeeded = FALSE;
    successResult = args->get_IsSuccess(&succeeded);
    if (!navigationMutationIsCurrent(capturedWebView.Get(),
                                     callbackMutationEpoch) ||
        activeNavigationId != capturedActiveNavigationId ||
        activeNavigationRevision != capturedNavigationRevision) {
      return S_OK;
    }
    if (FAILED(successResult)) return failInitialNavigation(successResult);
    if (succeeded == FALSE) return failInitialNavigation(E_FAIL);

    navigationComplete = true;
    activeNavigationSourceCommitted = true;
    const HRESULT flushResult = flushPendingHostMessagesForCurrentNavigation();
    if (flushResult == S_FALSE) return S_OK;
    if (FAILED(flushResult)) return flushResult;
    const std::uint64_t completionMutationEpoch = navigationMutationEpoch;
    if (!navigationMutationIsCurrent(capturedWebView.Get(),
                                     completionMutationEpoch) ||
        activeNavigationId != capturedActiveNavigationId ||
        activeNavigationRevision != capturedNavigationRevision) {
      return S_OK;
    }
    (void)initialLoad.completeSuccessForNavigation(capturedActiveNavigationId);
    return S_OK;
  }

  HRESULT onNavigationStarting(
      ICoreWebView2* sender,
      ICoreWebView2NavigationStartingEventArgs* args) {
    if (FAILED(checkThread()) || closeInProgress || state != State::Ready ||
        sender == nullptr || args == nullptr || webView.Get() != sender) {
      return S_FALSE;
    }

    const auto keepAlive = shared_from_this();
    (void)keepAlive;
    const ComPtr<ICoreWebView2> capturedWebView(sender);
    const std::uint64_t callbackMutationEpoch = navigationMutationEpoch;

    // Keep an identity for the sole host-issued slot before calling any event
    // argument getter. A getter may synchronously re-enter navigate() or
    // close(); after that re-entry we can still retire exactly this slot (or
    // discover that the newer deferred request should take over).
    std::optional<NavigationGeneration> entryHostGeneration;
    bool entryHostExpectedStarting = true;
    if (issuedNavigation &&
        (issuedNavigation->phase == NativeNavigationPhase::Calling ||
         issuedNavigation->phase == NativeNavigationPhase::AwaitingStart)) {
      entryHostGeneration = issuedNavigation->request.generation;
      entryHostExpectedStarting = issuedNavigation->expectsNavigationStarting;
    }

    const auto navigationTargetIsCurrent = [&]() noexcept {
      return !closeInProgress && state == State::Ready &&
             capturedWebView.Get() != nullptr &&
             webView.Get() == capturedWebView.Get();
    };
    const auto abandonCurrentStart = [&](HRESULT cause) -> HRESULT {
      bool ownsIssued =
          entryHostGeneration &&
          issuedNavigationMatches(capturedWebView.Get(),
                                   *entryHostGeneration);
      bool replacementWaiting = deferredNavigation.has_value();
      if (ownsIssued) {
        if (entryHostExpectedStarting) {
          (void)initialLoad.cancelPendingNativeNavigation(
              *entryHostGeneration);
        }
        issuedNavigation.reset();
      }
      if (ownsIssued || replacementWaiting) {
        activeNavigationId = 0U;
        activeNavigationSourceCommitted = false;
        activeNavigationDocumentKey.clear();
        navigationComplete = false;
        initialLoad.retireActiveNavigation();
        advanceActiveNavigationRevision();
      }
      navigationDriveRequested = replacementWaiting;

      const std::uint64_t cancelMutationEpoch = navigationMutationEpoch;
      const HRESULT cancelResult = args->put_Cancel(TRUE);
      replacementWaiting = deferredNavigation.has_value();
      const bool cancellationMutationIsCurrent =
          navigationMutationIsCurrent(capturedWebView.Get(),
                                       cancelMutationEpoch);
      if (!cancellationMutationIsCurrent && !replacementWaiting) {
        return S_OK;
      }
      if (replacementWaiting) {
        navigationDriveRequested = true;
        return S_OK;
      }
      const HRESULT failure = FAILED(cancelResult)
                                  ? cancelResult
                                  : (FAILED(cause) ? cause : E_FAIL);
      (void)failInitialNavigation(failure);
      return FAILED(cancelResult) ? cancelResult : cause;
    };
    const auto staleAfterGetter = [&](HRESULT cause)
        -> std::optional<HRESULT> {
      const bool ownsIssued =
          entryHostGeneration &&
          issuedNavigationMatches(capturedWebView.Get(),
                                   *entryHostGeneration);
      const auto action = detail::navigationStartAfterGetterAction(
          navigationTargetIsCurrent(),
          navigationMutationEpoch == callbackMutationEpoch, ownsIssued,
          deferredNavigation.has_value());
      switch (action) {
        case detail::NavigationStartAfterGetterAction::Continue:
          return std::nullopt;
        case detail::NavigationStartAfterGetterAction::Ignore:
          return S_OK;
        case detail::NavigationStartAfterGetterAction::AbandonCurrentStart:
          return abandonCurrentStart(cause);
        case detail::NavigationStartAfterGetterAction::CancelStaleEvent:
          // A nested request can already have moved from deferred to issued,
          // or even consumed the issued slot, before the getter returns. The
          // outer event is still stale. Cancellation is best-effort: its
          // failure must not fail or overwrite the newer request.
          (void)args->put_Cancel(TRUE);
          if (issuedNavigation || deferredNavigation) {
            navigationDriveRequested = true;
          }
          return S_OK;
      }
      return S_OK;
    };

    UINT64 navigationId = 0U;
    const HRESULT idResult = args->get_NavigationId(&navigationId);
    if (auto stale = staleAfterGetter(idResult); stale) return *stale;
    if (!navigationTargetIsCurrent()) return S_OK;
    if (FAILED(idResult) || navigationId == 0U) {
      const HRESULT failure = FAILED(idResult) ? idResult : E_UNEXPECTED;
      return abandonCurrentStart(failure);
    }

    BOOL isRedirected = FALSE;
    const HRESULT redirectResult = args->get_IsRedirected(&isRedirected);
    if (auto stale = staleAfterGetter(redirectResult); stale) return *stale;
    if (!navigationTargetIsCurrent()) return S_OK;
    if (FAILED(redirectResult)) {
      return abandonCurrentStart(redirectResult);
    }

    std::optional<NavigationGeneration> hostGeneration;
    bool hostExpectedStarting = true;
    if (isRedirected == FALSE && entryHostGeneration &&
        issuedNavigationMatches(capturedWebView.Get(),
                                 *entryHostGeneration)) {
      hostGeneration = entryHostGeneration;
      hostExpectedStarting = entryHostExpectedStarting;
    }

    LPWSTR rawUri = nullptr;
    const HRESULT uriResult = args->get_Uri(&rawUri);
    if (auto stale = staleAfterGetter(uriResult); stale) {
      CoTaskMemFree(rawUri);
      return *stale;
    }
    if (!navigationTargetIsCurrent()) {
      CoTaskMemFree(rawUri);
      return S_OK;
    }
    if (FAILED(uriResult) || rawUri == nullptr) {
      CoTaskMemFree(rawUri);
      const HRESULT failure = FAILED(uriResult) ? uriResult : E_POINTER;
      return abandonCurrentStart(failure);
    }

    const auto uriView = boundedWideView(
        rawUri, detail::kWebView2MaxNavigationCodeUnits);
    if (!uriView) {
      CoTaskMemFree(rawUri);
      return abandonCurrentStart(E_INVALIDARG);
    }

    const bool allowed = isAllowedNavigationForOptions(*uriView, options);
    if (auto stale = staleAfterGetter(E_UNEXPECTED); stale) {
      CoTaskMemFree(rawUri);
      return *stale;
    }
    if (!navigationTargetIsCurrent()) {
      CoTaskMemFree(rawUri);
      return S_OK;
    }

    std::wstring externalDocumentKey;
    HRESULT canonicalResult = S_OK;
    if (allowed && !hostGeneration) {
      canonicalResult = detail::canonicalDocumentUri(
          *uriView, externalDocumentKey);
    }
    CoTaskMemFree(rawUri);
    if (auto stale = staleAfterGetter(canonicalResult); stale) return *stale;
    if (!navigationTargetIsCurrent()) return S_OK;

    // A valid request that arrived while this event was being dispatched is
    // the only page the host still wants. Retire this start before asking
    // WebView2 to cancel it; put_Cancel may itself synchronously dispatch the
    // superseded completion.
    bool replacementWaiting = deferredNavigation.has_value();
    const bool deniedHostStart = !allowed && hostGeneration.has_value();
    const bool deniedActiveRedirect =
        !allowed && isRedirected != FALSE &&
        activeNavigationId == navigationId;
    const bool cancelForReplacement =
        replacementWaiting &&
        (isRedirected == FALSE || activeNavigationId == navigationId);
    if (cancelForReplacement || deniedHostStart || deniedActiveRedirect) {
      if (hostGeneration &&
          issuedNavigationMatches(capturedWebView.Get(), *hostGeneration)) {
        if (hostExpectedStarting) {
          (void)initialLoad.cancelPendingNativeNavigation(*hostGeneration);
        }
        issuedNavigation.reset();
      }
      activeNavigationId = 0U;
      activeNavigationSourceCommitted = false;
      activeNavigationDocumentKey.clear();
      navigationComplete = false;
      initialLoad.retireActiveNavigation();
      advanceActiveNavigationRevision();
      navigationDriveRequested = replacementWaiting;

      const std::uint64_t cancelMutationEpoch = navigationMutationEpoch;
      const HRESULT cancelResult = args->put_Cancel(TRUE);
      replacementWaiting = deferredNavigation.has_value();
      const bool cancellationMutationIsCurrent =
          navigationMutationIsCurrent(capturedWebView.Get(),
                                       cancelMutationEpoch);
      if (!cancellationMutationIsCurrent && !replacementWaiting) {
        return S_OK;
      }
      if (replacementWaiting) {
        // The replacement will issue after this callback unwinds. A failed
        // best-effort cancellation does not make the obsolete page terminal.
        navigationDriveRequested = true;
        return S_OK;
      }
      const HRESULT failure = FAILED(cancelResult)
                                  ? cancelResult
                                  : E_ACCESSDENIED;
      (void)failInitialNavigation(failure);
      return FAILED(cancelResult) ? cancelResult : S_OK;
    }

    if (!allowed) {
      // A denied external top-level attempt was never admitted by the host.
      // Cancel it without retiring the still-usable current document.
      const std::uint64_t cancelMutationEpoch = navigationMutationEpoch;
      const HRESULT cancelResult = args->put_Cancel(TRUE);
      const bool replacementWaiting = deferredNavigation.has_value();
      const bool cancellationMutationIsCurrent =
          navigationMutationIsCurrent(capturedWebView.Get(),
                                       cancelMutationEpoch);
      if (replacementWaiting && navigationTargetIsCurrent()) {
        navigationDriveRequested = true;
        return S_OK;
      }
      if (FAILED(cancelResult) &&
          cancellationMutationIsCurrent) {
        (void)failInitialNavigation(cancelResult);
      }
      return cancelResult;
    }
    if (FAILED(canonicalResult)) {
      return abandonCurrentStart(canonicalResult);
    }

    bool accepted = false;
    std::wstring documentKey;
    if (hostGeneration &&
        issuedNavigationMatches(capturedWebView.Get(), *hostGeneration)) {
      documentKey = std::move(issuedNavigation->request.documentKey);
      accepted = hostExpectedStarting
                     ? initialLoad.acceptNavigationForRequest(
                           navigationId, *hostGeneration)
                     : initialLoad.acceptNavigation(navigationId);
      issuedNavigation.reset();
      navigationDriveRequested = true;
      if (!accepted) {
        const HRESULT failure = E_UNEXPECTED;
        (void)failInitialNavigation(failure);
        return failure;
      }
    } else if (isRedirected != FALSE) {
      accepted = activeNavigationId == navigationId &&
                 initialLoad.acceptNavigation(navigationId, true);
      if (accepted) {
        documentKey = std::move(externalDocumentKey);
      }
    } else {
      accepted = initialLoad.acceptNavigation(navigationId);
      documentKey = std::move(externalDocumentKey);
    }

    if (!accepted) return S_OK;
    activeNavigationId = navigationId;
    navigationComplete = false;
    activeNavigationSourceCommitted = false;
    advanceActiveNavigationRevision();
    activeNavigationDocumentKey.swap(documentKey);
    return S_OK;
  }

  HRESULT configureSecurity(ICoreWebView2* setupWebView) {
    if (setupWebView == nullptr) return E_POINTER;
    ComPtr<ICoreWebView2Settings> settings;
    HRESULT hr = setupWebView->get_Settings(settings.GetAddressOf());
    if (closeInProgress || state != State::Initializing ||
        webView.Get() != setupWebView) {
      return S_FALSE;
    }
    if (FAILED(hr) || !settings) return FAILED(hr) ? hr : E_POINTER;
    hr = firstFailure({
        settings->put_AreDefaultScriptDialogsEnabled(FALSE),
        settings->put_AreDevToolsEnabled(FALSE),
        settings->put_IsStatusBarEnabled(FALSE),
        settings->put_IsZoomControlEnabled(FALSE),
        settings->put_AreHostObjectsAllowed(FALSE),
    });
    if (closeInProgress || state != State::Initializing ||
        webView.Get() != setupWebView) {
      return S_FALSE;
    }
    return hr;
  }

  HRESULT configureVirtualHosts(
      ICoreWebView2* setupWebView,
      ComPtr<ICoreWebView2_3>& configuredWebView3,
      bool& configuredCanvasMapped,
      bool& configuredMediaMapped) {
    configuredWebView3.Reset();
    configuredCanvasMapped = false;
    configuredMediaMapped = false;
    if (!options.canvasLocalFolder && !options.mediaCanvasLocalFolder) {
      return S_OK;
    }

    if (setupWebView == nullptr) return E_POINTER;
    ComPtr<ICoreWebView2> capturedWebView(setupWebView);
    ComPtr<ICoreWebView2_3> localWebView3;
    HRESULT hr = capturedWebView.As(&localWebView3);
    if (closeInProgress || state != State::Initializing ||
        webView.Get() != setupWebView) {
      return S_FALSE;
    }
    if (FAILED(hr) || !localWebView3) return FAILED(hr) ? hr : E_NOINTERFACE;
    configuredWebView3 = localWebView3;
    std::wstring executableFolder;
    hr = detail::executableDirectory(executableFolder);
    if (FAILED(hr)) return hr;

    const auto mapFolder = [this, &executableFolder, &localWebView3,
                            setupWebView](
                               LPCWSTR hostName,
                               const std::optional<std::wstring>& folder,
                               bool packagedCanvasFolder,
                               bool& mapped) -> HRESULT {
      if (!folder) return S_OK;
      std::wstring normalizedFolder;
      HRESULT result = packagedCanvasFolder
                           ? detail::normalizePackagedCanvasFolder(
                                 *folder, executableFolder, normalizedFolder)
                           : detail::normalizeVirtualHostFolder(
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
      result = localWebView3->SetVirtualHostNameToFolderMapping(
          hostName, normalizedFolder.c_str(),
          COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
      if (closeInProgress || state != State::Initializing ||
          webView.Get() != setupWebView) {
        if (SUCCEEDED(result)) {
          const HRESULT clearResult =
              localWebView3->ClearVirtualHostNameToFolderMapping(hostName);
          mapped = FAILED(clearResult);
        }
        return S_FALSE;
      }
      if (SUCCEEDED(result)) mapped = true;
      return result;
    };

    const auto rollbackMappings = [&]() noexcept {
      if (configuredMediaMapped) {
        const HRESULT result =
            localWebView3->ClearVirtualHostNameToFolderMapping(
                L"media.canvas.local");
        if (SUCCEEDED(result)) configuredMediaMapped = false;
      }
      if (configuredCanvasMapped) {
        const HRESULT result =
            localWebView3->ClearVirtualHostNameToFolderMapping(
                L"canvas.local");
        if (SUCCEEDED(result)) configuredCanvasMapped = false;
      }
    };

    hr = mapFolder(L"canvas.local", options.canvasLocalFolder, true,
                   configuredCanvasMapped);
    if (hr != S_OK) {
      rollbackMappings();
      return hr;
    }
    hr = mapFolder(L"media.canvas.local", options.mediaCanvasLocalFolder,
                   false, configuredMediaMapped);
    if (hr != S_OK) {
      rollbackMappings();
      return hr;
    }
    return S_OK;
  }

  HRESULT registerEventHandlers(ICoreWebView2* setupWebView) {
    if (setupWebView == nullptr) return E_POINTER;
    const ComPtr<ICoreWebView2> capturedSetupWebView(setupWebView);
    const auto setupIsCurrent = [this, setupWebView]() noexcept {
      return !closeInProgress && state == State::Initializing &&
             webView.Get() == setupWebView;
    };
    const std::weak_ptr<Impl> weak = weak_from_this();
    auto navigation = Callback<ICoreWebView2NavigationStartingEventHandler>(
        [weak](ICoreWebView2* sender,
               ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
          const auto self = weak.lock();
          if (!self) return S_OK;

          // Do not issue a replacement native Navigate from inside this COM
          // event. Synchronous events are drained by the outer Navigate call;
          // asynchronous events drain immediately after the handler unwinds.
          const bool wasBlocked = self->navigationStartDispatchBlocked;
          self->navigationStartDispatchBlocked = true;
          HRESULT result = S_OK;
          try {
            result = self->onNavigationStarting(sender, args);
          } catch (...) {
            result = self->failInitialNavigation(E_OUTOFMEMORY);
          }
          self->navigationStartDispatchBlocked = wasBlocked;
          if (!wasBlocked && self->navigationDriveRequested &&
              !self->closeInProgress && self->state == State::Ready) {
            (void)self->driveNavigation();
          }
          return result;
        });
    if (!navigation) return E_OUTOFMEMORY;
    EventRegistrationToken addedToken{};
    HRESULT hr = setupWebView->add_NavigationStarting(navigation.Get(),
                                                       &addedToken);
    if (FAILED(hr)) return hr;
    if (!setupIsCurrent()) {
      (void)capturedSetupWebView->remove_NavigationStarting(addedToken);
      return S_FALSE;
    }
    navigationToken = addedToken;
    hasNavigationToken = true;

    auto sourceChanged = Callback<ICoreWebView2SourceChangedEventHandler>(
        [weak](ICoreWebView2* sender,
               ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
          const auto self = weak.lock();
          if (!self || args == nullptr || sender == nullptr) return S_OK;
          const ComPtr<ICoreWebView2> capturedWebView(sender);
          if (self->closeInProgress || self->state != State::Ready ||
              self->webView.Get() != capturedWebView.Get() ||
              self->issuedNavigation || self->deferredNavigation) {
            return S_OK;
          }
          const UINT64 capturedNavigationId = self->activeNavigationId;
          const std::uint64_t capturedNavigationRevision =
              self->activeNavigationRevision;
          const std::uint64_t callbackMutationEpoch =
              self->navigationMutationEpoch;
          const auto failSourceOrDefer = [&](HRESULT failure) -> HRESULT {
            if (self->deferredNavigation) {
              self->navigationDriveRequested = true;
              if (!self->navigationDriveActive &&
                  !self->navigationStartDispatchBlocked) {
                (void)self->driveNavigation();
              }
              return S_OK;
            }
            (void)self->failInitialNavigation(failure);
            return failure;
          };
          BOOL isNewDocument = FALSE;
          const HRESULT sourceResult =
              args->get_IsNewDocument(&isNewDocument);
          if (!self->navigationMutationIsCurrent(
                  capturedWebView.Get(), callbackMutationEpoch) ||
              (capturedNavigationId != 0U &&
               !self->sourceChangeIsCurrent(
                   capturedWebView.Get(), capturedNavigationId,
                   capturedNavigationRevision))) {
            return S_OK;
          }
          if (FAILED(sourceResult)) {
            return failSourceOrDefer(sourceResult);
          }
          if (isNewDocument != FALSE && capturedNavigationId != 0U) {
            const UINT64 navigationId = capturedNavigationId;
            const std::uint64_t navigationRevision =
                capturedNavigationRevision;
            LPWSTR rawSource = nullptr;
            const HRESULT currentSourceResult =
                capturedWebView->get_Source(&rawSource);
            if (!self->navigationMutationIsCurrent(
                    capturedWebView.Get(), callbackMutationEpoch) ||
                !self->sourceChangeIsCurrent(capturedWebView.Get(),
                                             navigationId,
                                             navigationRevision)) {
              CoTaskMemFree(rawSource);
              return S_OK;
            }
            if (FAILED(currentSourceResult) || rawSource == nullptr) {
              CoTaskMemFree(rawSource);
              const HRESULT failure = FAILED(currentSourceResult)
                                          ? currentSourceResult
                                          : E_POINTER;
              return failSourceOrDefer(failure);
            }

            const auto sourceView = boundedWideView(
                rawSource, detail::kWebView2MaxNavigationCodeUnits);
            if (!sourceView) {
              CoTaskMemFree(rawSource);
              return failSourceOrDefer(E_INVALIDARG);
            }

            std::wstring sourceDocumentKey;
            const HRESULT canonicalResult = detail::canonicalDocumentUri(
                *sourceView, sourceDocumentKey);
            CoTaskMemFree(rawSource);
            if (!self->navigationMutationIsCurrent(
                    capturedWebView.Get(), callbackMutationEpoch) ||
                !self->sourceChangeIsCurrent(capturedWebView.Get(),
                                             navigationId,
                                             navigationRevision)) {
              return S_OK;
            }
            if (FAILED(canonicalResult)) {
              return failSourceOrDefer(canonicalResult);
            }
            self->activeNavigationSourceCommitted =
                sourceDocumentKey == self->activeNavigationDocumentKey;
            self->advanceNavigationMutationEpoch();
          }
          return S_OK;
        });
    if (!sourceChanged) return E_OUTOFMEMORY;
    addedToken = {};
    hr = setupWebView->add_SourceChanged(sourceChanged.Get(), &addedToken);
    if (FAILED(hr)) return hr;
    if (!setupIsCurrent()) {
      (void)capturedSetupWebView->remove_SourceChanged(addedToken);
      return S_FALSE;
    }
    sourceChangedToken = addedToken;
    hasSourceChangedToken = true;

    auto navigationCompleted =
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [weak](ICoreWebView2* sender,
                   ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
              const auto self = weak.lock();
              return self ? self->onNavigationCompleted(sender, args) : S_OK;
            });
    if (!navigationCompleted) return E_OUTOFMEMORY;
    addedToken = {};
    hr = setupWebView->add_NavigationCompleted(navigationCompleted.Get(),
                                                &addedToken);
    if (FAILED(hr)) return hr;
    if (!setupIsCurrent()) {
      (void)capturedSetupWebView->remove_NavigationCompleted(addedToken);
      return S_FALSE;
    }
    navigationCompletedToken = addedToken;
    hasNavigationCompletedToken = true;

    auto newWindow = Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [weak](ICoreWebView2*,
               ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
          if (weak.expired() || args == nullptr) return S_OK;
          return args->put_Handled(TRUE);
        });
    if (!newWindow) return E_OUTOFMEMORY;
    addedToken = {};
    hr = setupWebView->add_NewWindowRequested(newWindow.Get(), &addedToken);
    if (FAILED(hr)) return hr;
    if (!setupIsCurrent()) {
      (void)capturedSetupWebView->remove_NewWindowRequested(addedToken);
      return S_FALSE;
    }
    newWindowToken = addedToken;
    hasNewWindowToken = true;

    auto permission = Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [weak](ICoreWebView2*,
               ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
          if (weak.expired() || args == nullptr) return S_OK;
          return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
        });
    if (!permission) return E_OUTOFMEMORY;
    addedToken = {};
    hr = setupWebView->add_PermissionRequested(permission.Get(), &addedToken);
    if (FAILED(hr)) return hr;
    if (!setupIsCurrent()) {
      (void)capturedSetupWebView->remove_PermissionRequested(addedToken);
      return S_FALSE;
    }
    permissionToken = addedToken;
    hasPermissionToken = true;

    auto webMessage = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [weak](ICoreWebView2*,
               ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
          const auto self = weak.lock();
          if (!self || args == nullptr) return S_OK;
          LPWSTR value = nullptr;
          HRESULT messageResult = args->TryGetWebMessageAsString(&value);
          if (SUCCEEDED(messageResult) && value != nullptr) {
            self->captureMessage(value);
            CoTaskMemFree(value);
            return S_OK;
          }
          CoTaskMemFree(value);
          value = nullptr;
          messageResult = args->get_WebMessageAsJson(&value);
          if (SUCCEEDED(messageResult) && value != nullptr) {
            self->captureMessage(value);
          }
          CoTaskMemFree(value);
          return S_OK;
        });
    if (!webMessage) return E_OUTOFMEMORY;
    addedToken = {};
    hr = setupWebView->add_WebMessageReceived(webMessage.Get(), &addedToken);
    if (FAILED(hr)) return hr;
    if (!setupIsCurrent()) {
      (void)capturedSetupWebView->remove_WebMessageReceived(addedToken);
      return S_FALSE;
    }
    webMessageToken = addedToken;
    hasWebMessageToken = true;
    return S_OK;
  }

  HRESULT checkThread() const {
    return GetCurrentThreadId() == ownerThread ? S_OK : RPC_E_WRONG_THREAD;
  }

  void captureMessage(const wchar_t* value) noexcept {
    if (value == nullptr) return;
    try {
      // Bound the scan as well as the retained copy. WebView2 owns the input
      // allocation; an oversized inbound message is simply dropped.
      constexpr std::size_t kMax =
          detail::WebView2MessageLog::maxMessageCodeUnits();
      std::size_t length = 0U;
      while (length <= kMax && value[length] != L'\0') ++length;
      if (length > kMax) return;
      (void)capturedMessages.tryPush(std::wstring_view(value, length));
    } catch (...) {
      // Diagnostics must never be able to terminate the WebView callback.
    }
  }

  HRESULT remember(HRESULT result) {
    if (FAILED(result)) lastResult = result;
    return result;
  }

  HRESULT fail(HRESULT result) {
    const HRESULT failure = FAILED(result) ? result : E_FAIL;
    // A synchronous completion handler may destroy the owning surface. Keep
    // the PImpl alive through notification and return only the local result.
    const auto keepAlive = shared_from_this();
    (void)keepAlive;
    navigationRequestGenerations.invalidate();
    advanceActiveNavigationRevision();
    clearNavigationDriveState();
    lastResult = failure;
    state = State::Failed;
    (void)initialLoad.completeFailure(static_cast<std::int32_t>(failure));
    return failure;
  }

  HRESULT failInitialNavigation(HRESULT result) {
    const HRESULT failure = FAILED(result) ? result : E_FAIL;
    return fail(failure);
  }

  static InitialLoadState toPublicInitialLoadState(
      detail::InitialLoadState state) noexcept {
    switch (state) {
      case detail::InitialLoadState::NotRequested:
        return InitialLoadState::NotRequested;
      case detail::InitialLoadState::Pending:
        return InitialLoadState::Pending;
      case detail::InitialLoadState::Ready:
        return InitialLoadState::Ready;
      case detail::InitialLoadState::Failed:
        return InitialLoadState::Failed;
    }
    std::terminate();
  }

  HRESULT removeEventHandlers() noexcept override {
    HRESULT firstResult = S_OK;
    const auto record = [&firstResult](HRESULT result) {
      if (SUCCEEDED(firstResult) && FAILED(result)) firstResult = result;
    };
    if (hasNavigationCompletedToken) {
      if (webView) {
        record(webView->remove_NavigationCompleted(navigationCompletedToken));
      }
      hasNavigationCompletedToken = false;
    }
    if (hasSourceChangedToken) {
      if (webView) {
        record(webView->remove_SourceChanged(sourceChangedToken));
      }
      hasSourceChangedToken = false;
    }
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
  bool closeInProgress = false;
  HRESULT lastResult = S_OK;
  HRESULT closeResult = S_OK;
  Options options;
  RECT bounds{0, 0, 1, 1};
  bool visible = true;
  bool interactive = false;
  std::optional<PendingNavigationRequest> pendingNavigation;
  detail::WebView2PendingMessageQueue pendingHostMessages;
  detail::WebView2MessageLog capturedMessages;
  detail::InitialLoadTracker initialLoad;
  detail::NavigationRequestGenerationTracker navigationRequestGenerations;
  std::optional<IssuedNavigation> issuedNavigation;
  std::optional<PendingNavigationRequest> deferredNavigation;
  bool navigationDriveActive = false;
  bool navigationDriveRequested = false;
  bool navigationStartDispatchBlocked = false;
  bool navigationComplete = false;
  bool activeNavigationSourceCommitted = false;
  std::wstring activeNavigationDocumentKey;
  UINT64 activeNavigationId = 0;
  std::uint64_t activeNavigationRevision = 0U;
  std::uint64_t navigationMutationEpoch = 0U;

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
  EventRegistrationToken sourceChangedToken{};
  EventRegistrationToken navigationCompletedToken{};
  EventRegistrationToken newWindowToken{};
  EventRegistrationToken permissionToken{};
  EventRegistrationToken webMessageToken{};
  bool hasNavigationToken = false;
  bool hasSourceChangedToken = false;
  bool hasNavigationCompletedToken = false;
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

HRESULT WebView2Surface::setInitialLoadCompletionHandler(
    InitialLoadCompletionHandler handler) {
  return impl_->setInitialLoadCompletionHandler(std::move(handler));
}

HRESULT WebView2Surface::postMessage(std::wstring_view message) {
  return impl_->postMessage(message);
}

HRESULT WebView2Surface::focus() { return impl_->focus(); }

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

HRESULT WebView2Surface::setBoundsChecked(core::Rect bounds) {
  const HRESULT threadResult = impl_->checkThread();
  if (FAILED(threadResult)) return threadResult;
  return impl_->setBounds(bounds);
}

HRESULT WebView2Surface::setVisibleChecked(bool visible) {
  const HRESULT threadResult = impl_->checkThread();
  if (FAILED(threadResult)) return threadResult;
  return impl_->setVisible(visible);
}

void WebView2Surface::setBounds(core::Rect bounds) {
  (void)setBoundsChecked(bounds);
}

void WebView2Surface::setInteractive(bool interactive) {
  impl_->setInteractive(interactive);
}

void WebView2Surface::setVisible(bool visible) {
  (void)setVisibleChecked(visible);
}

WebView2Surface::State WebView2Surface::state() const noexcept {
  impl_->requireOwnerThread();
  return impl_->state;
}

WebView2Surface::InitialLoadState WebView2Surface::initialLoadState() const
    noexcept {
  impl_->requireOwnerThread();
  return Impl::toPublicInitialLoadState(impl_->initialLoad.state());
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
  return impl_->capturedMessages.values();
}

WebView2Surface::NavigationClass WebView2Surface::classifyNavigation(
    std::wstring_view uri, bool allowTestDataUrls) {
  return classifyNavigationImpl(uri, allowTestDataUrls);
}

}  // namespace canvas::windows
