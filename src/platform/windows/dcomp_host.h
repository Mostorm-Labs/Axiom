#pragma once

#include <array>
#include <cstddef>

#include <dcomp.h>
#include <windows.h>
#include <wrl/client.h>

namespace canvas::windows {

enum class VisualSlot {
  BaseCanvas = 0,
  EmbeddedContent = 1,
  Annotation = 2,
  InteractionChrome = 3,
};

class DCompHost {
 public:
  static constexpr std::array<VisualSlot, 4> visualOrder() {
    return {VisualSlot::BaseCanvas, VisualSlot::EmbeddedContent,
            VisualSlot::Annotation, VisualSlot::InteractionChrome};
  }

  HRESULT initialize(HWND window);
  HRESULT setContent(VisualSlot slot, IUnknown* content);
  HRESULT createChildVisual(VisualSlot parent, IDCompositionVisual** child);
  HRESULT commit();

  IDCompositionDevice* device() const { return device_.Get(); }
  IDCompositionVisual* visual(VisualSlot slot) const;

 private:
  static constexpr std::size_t kLayerCount = 4;

  Microsoft::WRL::ComPtr<IDCompositionDevice> device_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
  std::array<Microsoft::WRL::ComPtr<IDCompositionVisual>, kLayerCount> layers_;
};

}  // namespace canvas::windows
