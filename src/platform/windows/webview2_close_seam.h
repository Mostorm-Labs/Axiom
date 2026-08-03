#pragma once

#include <windows.h>

namespace canvas::windows::detail {

class WebView2CloseOperations {
 public:
  virtual ~WebView2CloseOperations() = default;

  virtual HRESULT removeEventHandlers() noexcept = 0;
  virtual HRESULT clearVirtualHostMappings() noexcept = 0;
  virtual HRESULT detachRootVisualTarget() noexcept = 0;
  virtual HRESULT hideController() noexcept = 0;
  virtual HRESULT closeController() noexcept = 0;
  virtual HRESULT removeChildVisual() noexcept = 0;
  virtual HRESULT commitComposition() noexcept = 0;
};

inline HRESULT runWebView2CloseOperations(
    WebView2CloseOperations& operations) noexcept {
  HRESULT firstResult = S_OK;
  const auto run = [&firstResult](HRESULT result) {
    if (SUCCEEDED(firstResult) && FAILED(result)) firstResult = result;
  };

  run(operations.removeEventHandlers());
  run(operations.clearVirtualHostMappings());
  run(operations.detachRootVisualTarget());
  run(operations.hideController());
  run(operations.closeController());
  run(operations.removeChildVisual());
  run(operations.commitComposition());
  return firstResult;
}

}  // namespace canvas::windows::detail
