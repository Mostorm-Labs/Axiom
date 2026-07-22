#include "platform/windows/dcomp_host.h"
#include "platform/windows/skia_d3d12_context.h"
#include "platform/windows/skia_swap_chain_layer.h"

#include <gtest/gtest.h>

namespace {

TEST(WindowsComposition, UsesFixedBackToFrontVisualOrder) {
    constexpr auto order = canvas::windows::DCompHost::visualOrder();
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], canvas::windows::VisualSlot::BaseCanvas);
    EXPECT_EQ(order[1], canvas::windows::VisualSlot::EmbeddedContent);
    EXPECT_EQ(order[2], canvas::windows::VisualSlot::Annotation);
    EXPECT_EQ(order[3], canvas::windows::VisualSlot::InteractionChrome);
}

TEST(WindowsComposition, CreatesTransparentAnnotationSwapChain) {
  const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ASSERT_TRUE(SUCCEEDED(comResult));
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = DefWindowProcW;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"CanvasTask11TestWindow";
  ASSERT_NE(RegisterClassW(&windowClass), 0u);
  HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"", WS_POPUP,
                               0, 0, 640, 480, nullptr, nullptr,
                               windowClass.hInstance, nullptr);
  ASSERT_NE(window, nullptr);

  canvas::windows::DCompHost host;
  ASSERT_TRUE(SUCCEEDED(host.initialize(window)));
  canvas::windows::SkiaD3D12Context gpu;
  ASSERT_TRUE(SUCCEEDED(gpu.initialize()));
  canvas::windows::SkiaSwapChainLayer annotation;
  ASSERT_TRUE(SUCCEEDED(annotation.initialize(
      gpu, host, canvas::windows::VisualSlot::Annotation, 640, 480, true)));
  EXPECT_EQ(annotation.alphaMode(), DXGI_ALPHA_MODE_PREMULTIPLIED);

  DestroyWindow(window);
  UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
  CoUninitialize();
}

}  // namespace
