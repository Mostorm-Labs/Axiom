#include "platform/windows/dcomp_host.h"

#include <utility>

namespace canvas::windows {

namespace {

constexpr std::size_t layerIndex(VisualSlot slot) {
  return static_cast<std::size_t>(slot);
}

bool isValidSlot(VisualSlot slot) {
  return layerIndex(slot) < DCompHost::visualOrder().size();
}

}  // namespace

HRESULT DCompHost::initialize(HWND window) {
  initialized_ = false;
  device_.Reset();
  target_.Reset();
  root_.Reset();
  for (auto& layer : layers_) {
    layer.Reset();
  }
  if (window == nullptr || !IsWindow(window)) {
    return E_INVALIDARG;
  }

  HRESULT hr = DCompositionCreateDevice(
      nullptr, IID_PPV_ARGS(device_.ReleaseAndGetAddressOf()));
  if (FAILED(hr)) {
    return hr;
  }

  hr = device_->CreateTargetForHwnd(window, TRUE,
                                    target_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return hr;
  }

  hr = device_->CreateVisual(root_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return hr;
  }

  for (auto& layer : layers_) {
    hr = device_->CreateVisual(layer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      return hr;
    }
  }

  // Add visuals in the fixed back-to-front order. The first child is inserted
  // at the front; each subsequent child is inserted after its predecessor.
  hr = root_->AddVisual(layers_[0].Get(), FALSE, nullptr);
  if (FAILED(hr)) {
    return hr;
  }
  for (std::size_t i = 1; i < layers_.size(); ++i) {
    hr = root_->AddVisual(layers_[i].Get(), TRUE, layers_[i - 1].Get());
    if (FAILED(hr)) {
      return hr;
    }
  }

  hr = target_->SetRoot(root_.Get());
  if (FAILED(hr)) {
    return hr;
  }
  hr = device_->Commit();
  if (SUCCEEDED(hr)) initialized_ = true;
  return hr;
}

IDCompositionVisual* DCompHost::visual(VisualSlot slot) const {
  if (!isValidSlot(slot)) {
    return nullptr;
  }
  return layers_[layerIndex(slot)].Get();
}

HRESULT DCompHost::setContent(VisualSlot slot, IUnknown* content) {
  if (!isValidSlot(slot)) {
    return E_INVALIDARG;
  }
  if (!initialized_) return E_UNEXPECTED;
  auto* layer = visual(slot);
  if (layer == nullptr) {
    return E_UNEXPECTED;
  }
  const HRESULT hr = layer->SetContent(content);
  return FAILED(hr) ? hr : commit();
}

HRESULT DCompHost::createChildVisual(VisualSlot parent,
                                     IDCompositionVisual** child) {
  if (child == nullptr) {
    return E_POINTER;
  }
  *child = nullptr;
  if (!isValidSlot(parent)) {
    return E_INVALIDARG;
  }
  if (!initialized_ || !device_) {
    return E_UNEXPECTED;
  }

  auto* parentVisual = visual(parent);
  if (parentVisual == nullptr) {
    return E_UNEXPECTED;
  }

  Microsoft::WRL::ComPtr<IDCompositionVisual> created;
  HRESULT hr = device_->CreateVisual(created.GetAddressOf());
  if (FAILED(hr)) {
    return hr;
  }
  hr = parentVisual->AddVisual(created.Get(), TRUE, nullptr);
  if (FAILED(hr)) {
    return hr;
  }
  hr = device_->Commit();
  if (FAILED(hr)) {
    // Keep the caller's out parameter null and roll back our graph ownership.
    // A best-effort commit of the rollback must not hide the original error.
    parentVisual->RemoveVisual(created.Get());
    device_->Commit();
    return hr;
  }

  *child = created.Detach();
  return S_OK;
}

HRESULT DCompHost::commit() {
  return initialized_ && device_ ? device_->Commit() : E_UNEXPECTED;
}

}  // namespace canvas::windows
