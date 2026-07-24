#include "platform/windows/dcomp_host.h"
#include "platform/windows/webview2_message_log.h"
#include "platform/windows/webview2_surface.h"

#include "canvas/input/input_router.h"

#include <gtest/gtest.h>
#include <wrl/client.h>

#include <chrono>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

class ScopedCom final {
 public:
  ScopedCom() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
  ~ScopedCom() {
    if (SUCCEEDED(result_)) CoUninitialize();
  }

  HRESULT result() const { return result_; }

 private:
  HRESULT result_;
};

class ScopedTestWindow final {
 public:
  ScopedTestWindow() {
    windowClass_.lpfnWndProc = DefWindowProcW;
    windowClass_.hInstance = GetModuleHandleW(nullptr);
    windowClass_.lpszClassName = L"CanvasTask12WebView2TestWindow";
    atom_ = RegisterClassW(&windowClass_);
    if (atom_ != 0) {
      window_ = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass_.lpszClassName,
                                L"", WS_POPUP, 0, 0, 640, 480, nullptr,
                                nullptr, windowClass_.hInstance, nullptr);
    }
  }

  ~ScopedTestWindow() {
    if (window_ != nullptr) DestroyWindow(window_);
    if (atom_ != 0) {
      UnregisterClassW(windowClass_.lpszClassName, windowClass_.hInstance);
    }
  }

  bool registered() const { return atom_ != 0; }
  HWND get() const { return window_; }

 private:
  WNDCLASSW windowClass_{};
  ATOM atom_ = 0;
  HWND window_ = nullptr;
};

template <typename Predicate>
bool pumpUntil(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

bool hasMessage(const canvas::windows::WebView2Surface& surface,
                std::wstring_view message) {
  for (const auto& captured : surface.capturedMessages()) {
    if (captured == message) return true;
  }
  return false;
}

TEST(WebView2Surface, ChildVisualRequiresAnInitializedHostAndValidSlot) {
  canvas::windows::DCompHost uninitialized;
  auto* child = reinterpret_cast<IDCompositionVisual*>(1);
  EXPECT_EQ(uninitialized.createChildVisual(
                canvas::windows::VisualSlot::EmbeddedContent, &child),
            E_UNEXPECTED);
  EXPECT_EQ(child, nullptr);
  EXPECT_EQ(uninitialized.createChildVisual(
                canvas::windows::VisualSlot::EmbeddedContent, nullptr),
            E_POINTER);

  ScopedCom com;
  ASSERT_TRUE(SUCCEEDED(com.result()));
  ScopedTestWindow testWindow;
  ASSERT_TRUE(testWindow.registered());
  ASSERT_NE(testWindow.get(), nullptr);
  canvas::windows::DCompHost host;
  ASSERT_TRUE(SUCCEEDED(host.initialize(testWindow.get())));

  child = reinterpret_cast<IDCompositionVisual*>(1);
  EXPECT_EQ(host.createChildVisual(
                static_cast<canvas::windows::VisualSlot>(99), &child),
            E_INVALIDARG);
  EXPECT_EQ(child, nullptr);

  Microsoft::WRL::ComPtr<IDCompositionVisual> ownedChild;
  ASSERT_TRUE(SUCCEEDED(host.createChildVisual(
      canvas::windows::VisualSlot::EmbeddedContent,
      ownedChild.GetAddressOf())));
  EXPECT_NE(ownedChild.Get(), nullptr);
}

TEST(WebView2Surface, ClassifiesNavigationWithoutHostPrefixSpoofing) {
  using NavigationClass = canvas::windows::WebView2Surface::NavigationClass;
  const auto classify =
      canvas::windows::WebView2Surface::classifyNavigation;

  EXPECT_EQ(classify(L"data:text/html,ok", false), NavigationClass::Denied);
  EXPECT_EQ(classify(L"data:text/html,ok", true), NavigationClass::TestData);
  EXPECT_EQ(classify(L"https://canvas.local/page", false),
            NavigationClass::LocalVirtualHost);
  EXPECT_EQ(classify(L"https://media.canvas.local/page", false),
            NavigationClass::LocalVirtualHost);
  EXPECT_EQ(classify(L"https://canvas.local.attacker.example/page", false),
            NavigationClass::Https);
  EXPECT_EQ(classify(L"https://canvas.local@attacker.example/page", false),
            NavigationClass::Https);
  EXPECT_EQ(classify(L"https://example.com/page", false),
            NavigationClass::Https);
  EXPECT_EQ(classify(L"http://canvas.local/", false),
            NavigationClass::Denied);
  EXPECT_EQ(classify(L"file:///C:/Windows/win.ini", false),
            NavigationClass::Denied);
  EXPECT_EQ(classify(L"javascript:alert(1)", false),
            NavigationClass::Denied);
  EXPECT_EQ(classify(L"about:blank", false), NavigationClass::Denied);
}

TEST(WebView2Surface, RejectsOversizedNativeMessagesBeforeRetainingThem) {
  canvas::windows::DCompHost host;
  canvas::windows::WebView2Surface surface(host, nullptr);
  const std::wstring oversizedMessage(
      canvas::windows::detail::kWebView2MaxMessageCodeUnits + 1U, L'x');
  const std::wstring oversizedNavigation(
      canvas::windows::detail::kWebView2MaxNavigationCodeUnits + 1U, L'x');

  EXPECT_EQ(surface.postMessage(oversizedMessage), E_INVALIDARG);
  EXPECT_EQ(surface.navigate(oversizedNavigation), E_INVALIDARG);
  EXPECT_TRUE(surface.capturedMessages().empty());
  EXPECT_EQ(surface.close(), S_OK);
}

TEST(WebView2Surface,
     CheckedSettersReturnCurrentResultWithoutClearingStickyFailure) {
  canvas::windows::DCompHost host;
  canvas::windows::WebView2Surface surface(host, nullptr);

  EXPECT_EQ(surface.state(),
            canvas::windows::WebView2Surface::State::Created);
  EXPECT_EQ(surface.setBoundsChecked(
                canvas::core::Rect{0.0F, 0.0F, -1.0F, 10.0F}),
            E_INVALIDARG);
  EXPECT_EQ(surface.lastResult(), E_INVALIDARG);

  EXPECT_EQ(surface.setBoundsChecked(
                canvas::core::Rect{0.0F, 0.0F, 10.0F, 10.0F}),
            S_OK);
  EXPECT_EQ(surface.setVisibleChecked(false), S_OK);
  EXPECT_EQ(surface.lastResult(), E_INVALIDARG);
  EXPECT_EQ(surface.state(),
            canvas::windows::WebView2Surface::State::Created);
  EXPECT_EQ(surface.close(), S_OK);
}

TEST(WebView2Surface, CheckedSettersReportWrongThread) {
  canvas::windows::DCompHost host;
  canvas::windows::WebView2Surface surface(host, nullptr);

  HRESULT boundsResult = S_OK;
  HRESULT visibleResult = S_OK;
  std::thread wrongThread([&] {
    boundsResult = surface.setBoundsChecked(
        canvas::core::Rect{0.0F, 0.0F, 10.0F, 10.0F});
    visibleResult = surface.setVisibleChecked(false);
  });
  wrongThread.join();

  EXPECT_EQ(boundsResult, RPC_E_WRONG_THREAD);
  EXPECT_EQ(visibleResult, RPC_E_WRONG_THREAD);
  EXPECT_EQ(surface.state(),
            canvas::windows::WebView2Surface::State::Created);
  EXPECT_EQ(surface.close(), S_OK);
}

TEST(WebView2Surface, CloseRejectsTheWrongApartmentWithoutReleasingOwnership) {
  ScopedCom com;
  ASSERT_TRUE(SUCCEEDED(com.result()));
  ScopedTestWindow testWindow;
  ASSERT_TRUE(testWindow.registered());
  ASSERT_NE(testWindow.get(), nullptr);
  canvas::windows::DCompHost host;
  ASSERT_TRUE(SUCCEEDED(host.initialize(testWindow.get())));
  canvas::windows::WebView2Surface surface(host, testWindow.get());

  HRESULT wrongThreadResult = S_OK;
  std::thread wrongThread(
      [&] { wrongThreadResult = surface.close(); });
  wrongThread.join();
  EXPECT_EQ(wrongThreadResult, RPC_E_WRONG_THREAD);
  EXPECT_EQ(surface.state(),
            canvas::windows::WebView2Surface::State::Created);
  EXPECT_EQ(surface.close(), S_OK);
  EXPECT_EQ(surface.state(), canvas::windows::WebView2Surface::State::Closed);
}

TEST(WebView2Surface, HostsContentBelowInkAndGatesSyntheticClicksByMode) {
  ScopedCom com;
  ASSERT_TRUE(SUCCEEDED(com.result()));
  ScopedTestWindow testWindow;
  ASSERT_TRUE(testWindow.registered());
  ASSERT_NE(testWindow.get(), nullptr);
  ShowWindow(testWindow.get(), SW_SHOW);
  ASSERT_TRUE(IsWindowVisible(testWindow.get()));

  canvas::windows::DCompHost host;
  ASSERT_TRUE(SUCCEEDED(host.initialize(testWindow.get())));

  canvas::windows::WebView2Surface::Options testOptions;
  testOptions.allowTestDataUrls = true;
  canvas::windows::WebView2Surface surface(host, testWindow.get(),
                                            std::move(testOptions));
  surface.setBounds(canvas::core::Rect{0.0F, 0.0F, 640.0F, 480.0F});
  ASSERT_TRUE(SUCCEEDED(surface.initialize()));
  EXPECT_EQ(surface.navigate(L"http://canvas.local/"), E_ACCESSDENIED);
  EXPECT_EQ(surface.navigate(L"file:///C:/Windows/win.ini"), E_ACCESSDENIED);
  EXPECT_EQ(surface.navigate(L"javascript:alert(1)"), E_ACCESSDENIED);
  EXPECT_EQ(surface.navigate(L"about:blank"), E_ACCESSDENIED);
  EXPECT_EQ(surface.navigate(L"https://canvas.local/unmapped"),
            E_ACCESSDENIED);

  constexpr std::wstring_view kPage =
      L"data:text/html,%3C!doctype%20html%3E%3Cmeta%20charset=utf-8%3E"
      L"%3Cstyle%3Ehtml,body%7Bwidth:100%25;height:100%25;margin:0%7D%3C/style%3E"
      L"%3Cbutton%20id=click-target%20style='position:absolute;left:16px;"
      L"top:16px;width:64px;height:64px'%3E"
      L"target%3C/button%3E"
      L"%3Cscript%3E"
      L"chrome.webview.addEventListener('message',e=>"
      L"chrome.webview.postMessage(%7Btype:'host-message',value:e.data%7D));"
      L"const%20clickTarget=document.getElementById('click-target');"
      L"clickTarget.addEventListener('mousedown',()=>"
      L"chrome.webview.postMessage(%7Btype:'pressed'%7D));"
      L"clickTarget.addEventListener('click',()=>"
      L"chrome.webview.postMessage(%7Btype:'clicked'%7D));"
      L"chrome.webview.postMessage(%7Btype:'ready'%7D);"
      L"%3C/script%3E";
  ASSERT_TRUE(SUCCEEDED(surface.navigate(kPage)));
  EXPECT_EQ(surface.postMessage(L""), E_INVALIDARG);
  ASSERT_TRUE(
      SUCCEEDED(surface.postMessage(LR"({"type":"queued-a"})")));
  // A second navigation starts a new host-message generation. The queued A
  // message must not leak into page B. Reuse the complete page contract so
  // page B retains the click handler exercised below; the encoded marker
  // makes this a document navigation rather than a fragment navigation.
  std::wstring pageB(kPage);
  pageB += L"%3C!--page-b--%3E";
  ASSERT_TRUE(SUCCEEDED(surface.navigate(pageB)));
  ASSERT_TRUE(
      SUCCEEDED(surface.postMessage(LR"({"type":"queued-b"})")));

  constexpr std::wstring_view kQueuedBHostMessage =
      LR"json({"type":"host-message","value":"{\"type\":\"queued-b\"}"})json";
  constexpr std::wstring_view kQueuedAHostMessage =
      LR"json({"type":"host-message","value":"{\"type\":\"queued-a\"}"})json";

  ASSERT_TRUE(pumpUntil(
      [&] {
        return surface.state() ==
                   canvas::windows::WebView2Surface::State::Ready &&
               hasMessage(surface, LR"({"type":"ready"})");
      },
      std::chrono::seconds(5)))
      << "WebView2 failed with HRESULT 0x" << std::hex
      << static_cast<unsigned long>(surface.lastResult());
  EXPECT_TRUE(pumpUntil(
      [&] {
        return hasMessage(surface, kQueuedBHostMessage) &&
               !hasMessage(surface, kQueuedAHostMessage);
      },
      std::chrono::seconds(2)));

  const LPARAM clickPoint = MAKELPARAM(32, 32);
  const std::optional<canvas::document::NodeId> embeddedId{"web-1"};
  canvas::input::InputRouter router;
  router.setActiveEmbeddedNode(embeddedId);
  router.setMode(canvas::input::InputMode::Draw);
  const auto drawRoute =
      router.route(canvas::input::PointerKind::Mouse, embeddedId);
  ASSERT_NE(drawRoute.target, canvas::input::InputTarget::EmbeddedSurface);
  surface.setInteractive(
      drawRoute.target == canvas::input::InputTarget::EmbeddedSurface);
  EXPECT_EQ(surface.forwardMouseMessage(WM_LBUTTONDOWN, MK_LBUTTON,
                                        clickPoint),
            S_FALSE);
  EXPECT_EQ(surface.forwardMouseMessage(WM_LBUTTONUP, 0, clickPoint), S_FALSE);
  EXPECT_FALSE(pumpUntil(
      [&] { return hasMessage(surface, LR"({"type":"clicked"})"); },
      std::chrono::milliseconds(100)));

  router.setMode(canvas::input::InputMode::Interact);
  const auto interactRoute =
      router.route(canvas::input::PointerKind::Mouse, embeddedId);
  ASSERT_EQ(interactRoute.target,
            canvas::input::InputTarget::EmbeddedSurface);
  surface.setInteractive(
      interactRoute.target == canvas::input::InputTarget::EmbeddedSurface);
  // Model native capture being stolen after WebView received DOWN. The
  // cancellation path sends LEAVE and a surface-local outside UP. Chromium
  // may still route a compatibility click to a common document ancestor, but
  // it must not activate the control that received DOWN. A subsequent normal
  // click proves the next session is not poisoned by the cancellation.
  ASSERT_TRUE(SUCCEEDED(surface.forwardMouseMessage(
      WM_LBUTTONDOWN, MK_LBUTTON, clickPoint)));
  ASSERT_TRUE(pumpUntil(
      [&] { return hasMessage(surface, LR"({"type":"pressed"})"); },
      std::chrono::seconds(2)));
  ASSERT_TRUE(SUCCEEDED(surface.cancelMouseButtons(
      canvas::windows::embeddedMouseButtonMask(
          canvas::windows::EmbeddedMouseButton::Left))));
  EXPECT_FALSE(pumpUntil(
      [&] { return hasMessage(surface, LR"({"type":"clicked"})"); },
      std::chrono::milliseconds(100)));

  ASSERT_TRUE(SUCCEEDED(
      surface.forwardMouseMessage(WM_MOUSEMOVE, 0, clickPoint)));
  ASSERT_TRUE(SUCCEEDED(surface.forwardMouseMessage(
      WM_LBUTTONDOWN, MK_LBUTTON, clickPoint)));
  ASSERT_TRUE(
      SUCCEEDED(surface.forwardMouseMessage(WM_LBUTTONUP, 0, clickPoint)));
  EXPECT_TRUE(pumpUntil(
      [&] { return hasMessage(surface, LR"({"type":"clicked"})"); },
      std::chrono::seconds(2)));
  EXPECT_EQ(surface.close(), S_OK);
  EXPECT_EQ(surface.close(), S_OK);
  EXPECT_EQ(surface.state(), canvas::windows::WebView2Surface::State::Closed);
}

}  // namespace
