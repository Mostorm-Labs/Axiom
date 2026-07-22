#include "whiteboard_app.h"

#include "canvas/document/embedded_transform.h"
#include "platform/windows/win_pointer_adapter.h"

#include <windows.h>

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <stdexcept>
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

int WhiteboardApp::run(HINSTANCE instance, int commandShow,
                       bool selfTestLayers) {
  lastError_ = S_OK;
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
  if (SUCCEEDED(layerResult) && selfTestLayers) {
    layerResult = embeddedLayer_.initialize(
        gpu_, composition_, VisualSlot::EmbeddedContent, kCanvasWidth,
        kCanvasHeight, true);
  }
  if (SUCCEEDED(layerResult) && selfTestLayers) {
    layerResult = chromeLayer_.initialize(
        gpu_, composition_, VisualSlot::InteractionChrome, kCanvasWidth,
        kCanvasHeight, true);
  }
  if (FAILED(layerResult)) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(layerResult);
  }
  if (selfTestLayers) {
    baseLayer_.setClearColorArgb(0xFF00AA00U);
    if (!populateSelfTestDocument()) {
      DestroyWindow(window);
      UnregisterClassW(kWindowClassName, instance);
      return hresultExitCode(E_FAIL);
    }
  }
  HRESULT renderResult =
      baseLayer_.render(document_, document::LayerClass::Base);
  if (SUCCEEDED(renderResult) && selfTestLayers) {
    renderResult = embeddedLayer_.render(
        document_, document::LayerClass::Embedded);
  }
  if (SUCCEEDED(renderResult)) {
    renderResult = annotationLayer_.render(
        document_, document::LayerClass::Annotation);
  }
  if (SUCCEEDED(renderResult) && selfTestLayers) {
    renderResult = chromeLayer_.render(document_, document::LayerClass::Chrome);
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
    PostQuitMessage(0);
    return 0;
  }

  if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE ||
      message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
    auto* app = reinterpret_cast<WhiteboardApp*>(GetWindowLongPtrW(
        window, GWLP_USERDATA));
    if (app == nullptr) {
      return DefWindowProcW(window, message, wParam, lParam);
    }

    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INPUT_TYPE pointerType{};
    if (!GetPointerType(pointerId, &pointerType)) {
      const HRESULT cancelResult = app->cancelActivePointer(pointerId);
      if (FAILED(cancelResult)) {
        app->lastError_ = cancelResult;
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      if (message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
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
      const HRESULT cancelResult = app->cancelActivePointer(pointerId);
      if (FAILED(cancelResult)) {
        app->lastError_ = cancelResult;
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      if (message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
        ReleaseCapture();
      }
      return DefWindowProcW(window, message, wParam, lParam);
    }

    if (samples.empty()) {
      const HRESULT cancelResult = app->cancelActivePointer(pointerId);
      if (FAILED(cancelResult)) {
        app->lastError_ = cancelResult;
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      if (message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
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

std::optional<document::NodeId> WhiteboardApp::hitEmbedded(
    core::Vec2 point) const {
  for (auto it = document_.nodes().rbegin(); it != document_.nodes().rend();
       ++it) {
    if (std::holds_alternative<document::EmbeddedNode>(it->payload) &&
        it->bounds.contains(point)) {
      return it->id;
    }
  }
  return std::nullopt;
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

}  // namespace canvas::windows
