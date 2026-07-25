#pragma once

#include <memory>

namespace canvas::macos {

// AppKit and Metal remain behind this C++ PImpl boundary. The native view is
// passed as an opaque pointer so this header remains safe for C++/Skia core
// callers and public cross-platform headers never need Objective-C types.
//
// MetalHost is main-thread-affine: construct, use, detach, and destroy it on
// AppKit's main thread. Destruction off that thread terminates deliberately,
// rather than releasing ARC-owned AppKit/Metal objects on an invalid thread or
// leaving the attached CAMetalLayer behind.
class MetalHost {
 public:
  MetalHost();
  ~MetalHost();
  MetalHost(const MetalHost&) = delete;
  MetalHost& operator=(const MetalHost&) = delete;
  MetalHost(MetalHost&&) = delete;
  MetalHost& operator=(MetalHost&&) = delete;

  bool attachToView(void* nativeView);
  void detachFromView();
  void resize(double widthInPoints, double heightInPoints,
              double backingScale);
  void invalidate();
  void drawIfNeeded();
  bool isReady() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::macos
