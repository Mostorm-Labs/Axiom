#include "whiteboard_app.h"

#include "platform/windows/win_pointer_adapter.h"

#include <windows.h>

#include <cstddef>
#include <optional>
#include <string>
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

}  // namespace

int WhiteboardApp::run(HINSTANCE instance, int commandShow) {
  if (instance == nullptr) {
    return hresultExitCode(E_INVALIDARG);
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
  inputRouter_.setFingerDrawEnabled(true);
  HRESULT layerResult = baseLayer_.initialize(
      gpu_, composition_, VisualSlot::BaseCanvas, kCanvasWidth, kCanvasHeight,
      false);
  if (SUCCEEDED(layerResult)) {
    layerResult = annotationLayer_.initialize(
        gpu_, composition_, VisualSlot::Annotation, kCanvasWidth,
        kCanvasHeight, true);
  }
  if (FAILED(layerResult)) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(layerResult);
  }
  HRESULT renderResult =
      baseLayer_.render(document_, document::LayerClass::Base);
  if (SUCCEEDED(renderResult)) {
    renderResult = annotationLayer_.render(
        document_, document::LayerClass::Annotation);
  }
  if (FAILED(renderResult)) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(renderResult);
  }

  ShowWindow(window, commandShow);
  UpdateWindow(window);

  MSG message{};
  int messageResult = 0;
  while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  UnregisterClassW(kWindowClassName, instance);
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
    PostQuitMessage(0);
    return 0;
  }

  if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE ||
      message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
    if (message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
      ReleaseCapture();
    }
    auto* app = reinterpret_cast<WhiteboardApp*>(GetWindowLongPtrW(
        window, GWLP_USERDATA));
    if (app == nullptr) {
      return DefWindowProcW(window, message, wParam, lParam);
    }

    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INPUT_TYPE pointerType{};
    if (!GetPointerType(pointerId, &pointerType)) {
      return DefWindowProcW(window, message, wParam, lParam);
    }

    const input::PointerPhase phase = pointerPhaseForMessage(message);
    std::vector<input::PointerSample> samples;
    if (pointerType == PT_PEN) {
      samples = WinPointerAdapter::readPenHistory(window, pointerId, phase);
    } else if (pointerType == PT_TOUCH) {
      samples = WinPointerAdapter::readTouchHistory(window, pointerId, phase);
    } else {
      return DefWindowProcW(window, message, wParam, lParam);
    }

    for (std::size_t index = 0; index < samples.size(); ++index) {
      // Win32 returns history oldest-first after normalization. Only the
      // newest record represents the DOWN/UP edge; preceding records are
      // coalesced movement and must not begin/finish the stroke repeatedly.
      if (index + 1 < samples.size() &&
          (phase == input::PointerPhase::Down ||
           phase == input::PointerPhase::Up)) {
        samples[index].phase = input::PointerPhase::Move;
      }
      app->onPointerSample(samples[index]);
    }

    if (message == WM_POINTERDOWN) {
      SetCapture(window);
    }
    return 0;
  }

  return DefWindowProcW(window, message, wParam, lParam);
}

void WhiteboardApp::onPointerSample(const input::PointerSample& sample) {
  const auto route = inputRouter_.route(sample.kind, std::nullopt);
  if (route.target != input::InputTarget::BaseCanvas &&
      route.target != input::InputTarget::Annotation) {
    return;
  }
  if (sample.phase == input::PointerPhase::Down) {
    if (activeStroke_) return;
    activeStroke_.emplace(4.0F);
    activePointerId_ = sample.pointerId;
    activeStroke_->begin(sample);
    activePreview_ = {};
    activePreview_.width = 4.0F;
    activePreview_.points.push_back(document::StrokePoint{
        sample.screenPosition, sample.pressure, sample.timestampMicros});
    activeStrokeId_ = "stroke-" + std::to_string(++strokeSerial_);
    document::Node previewNode;
    previewNode.id = activeStrokeId_;
    previewNode.layer = document::LayerClass::Annotation;
    previewNode.payload = activePreview_;
    document_.add(std::move(previewNode));
    return;
  }
  if (!activeStroke_ || activePointerId_ != sample.pointerId) return;
  if (sample.phase == input::PointerPhase::Move) {
    const stroke::StrokeUpdate update = activeStroke_->append(sample);
    if (update.accepted) {
      activePreview_.points.push_back(document::StrokePoint{
          sample.screenPosition, sample.pressure, sample.timestampMicros});
    }
    if (update.dirtyBounds.width > 0.0F && update.dirtyBounds.height > 0.0F) {
      if (auto* previewNode = document_.find(activeStrokeId_)) {
        previewNode->payload = activePreview_;
      }
      (void)annotationLayer_.render(document_,
                                    document::LayerClass::Annotation,
                                    update.dirtyBounds);
    }
    return;
  }
  if (sample.phase == input::PointerPhase::Cancel) {
    document_.erase(activeStrokeId_);
    activeStroke_.reset();
    activePointerId_.reset();
    activePreview_ = {};
    activeStrokeId_.clear();
    (void)annotationLayer_.render(document_, document::LayerClass::Annotation);
    return;
  }
  const stroke::StrokeUpdate finalUpdate = activeStroke_->append(sample);
  document::StrokeNode finished = activeStroke_->finish();
  const core::Rect finalDirty = activeStroke_->finishDirtyBounds();
  if (auto* node = document_.find(activeStrokeId_)) {
    node->payload = std::move(finished);
  }
  activeStroke_.reset();
  activePointerId_.reset();
  activePreview_ = {};
  activeStrokeId_.clear();
  std::optional<core::Rect> redraw;
  if (finalUpdate.dirtyBounds.width > 0.0F &&
      finalUpdate.dirtyBounds.height > 0.0F) {
    redraw = finalUpdate.dirtyBounds;
  }
  if (finalDirty.width > 0.0F && finalDirty.height > 0.0F) {
    redraw = redraw ? redraw->united(finalDirty) : finalDirty;
  }
  (void)annotationLayer_.render(document_, document::LayerClass::Annotation,
                                redraw);
}

}  // namespace canvas::windows
