#include "platform/windows/dcomp_host.h"
#include "platform/windows/skia_d3d12_context.h"
#include "platform/windows/skia_swap_chain_layer.h"

#include <gtest/gtest.h>

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
    windowClass_.lpszClassName = L"CanvasTask11TestWindow";
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

TEST(WindowsComposition, UsesFixedBackToFrontVisualOrder) {
    constexpr auto order = canvas::windows::DCompHost::visualOrder();
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], canvas::windows::VisualSlot::BaseCanvas);
    EXPECT_EQ(order[1], canvas::windows::VisualSlot::EmbeddedContent);
    EXPECT_EQ(order[2], canvas::windows::VisualSlot::Annotation);
    EXPECT_EQ(order[3], canvas::windows::VisualSlot::InteractionChrome);
}

TEST(WindowsComposition, CreatesTransparentAnnotationSwapChain) {
  ScopedCom com;
  ASSERT_TRUE(SUCCEEDED(com.result()));
  ScopedTestWindow testWindow;
  ASSERT_TRUE(testWindow.registered());
  ASSERT_NE(testWindow.get(), nullptr);

  {
    canvas::windows::DCompHost host;
    ASSERT_TRUE(SUCCEEDED(host.initialize(testWindow.get())));
    canvas::windows::SkiaD3D12Context gpu;
    ASSERT_TRUE(SUCCEEDED(gpu.initialize(true)));
    canvas::windows::SkiaSwapChainLayer annotation;
    ASSERT_TRUE(SUCCEEDED(annotation.initialize(
        gpu, host, canvas::windows::VisualSlot::Annotation, 640, 480, true)));
    EXPECT_EQ(annotation.alphaMode(), DXGI_ALPHA_MODE_PREMULTIPLIED);
    EXPECT_TRUE(annotation.bufferNeedsFullRedraw(0));
    EXPECT_TRUE(annotation.bufferNeedsFullRedraw(1));
    canvas::document::Document document;
    ASSERT_TRUE(SUCCEEDED(annotation.render(
        document, canvas::document::LayerClass::Annotation,
        canvas::core::Rect{10, 10, 20, 20})));
    EXPECT_FALSE(annotation.bufferNeedsFullRedraw(0));
    EXPECT_TRUE(annotation.bufferNeedsFullRedraw(1));
    ASSERT_TRUE(SUCCEEDED(annotation.render(
        document, canvas::document::LayerClass::Annotation,
        canvas::core::Rect{20, 20, 20, 20})));
    EXPECT_FALSE(annotation.bufferNeedsFullRedraw(0));
    EXPECT_FALSE(annotation.bufferNeedsFullRedraw(1));
    EXPECT_TRUE(annotation.bufferHasPendingDirty(0));
    ASSERT_TRUE(SUCCEEDED(annotation.render(
        document, canvas::document::LayerClass::Annotation, std::nullopt)));
    EXPECT_FALSE(annotation.bufferNeedsFullRedraw(0));
    EXPECT_TRUE(annotation.bufferNeedsFullRedraw(1));
    EXPECT_FALSE(annotation.bufferHasPendingDirty(0));
    EXPECT_EQ(annotation.frameId(), 3u);
    if (annotation.mediaPresentCount() != 0) {
      EXPECT_LE(annotation.mediaFrameId(), annotation.frameId());
      EXPECT_GT(annotation.mediaFrameId(), 0u);
    }
  }

}

}  // namespace
