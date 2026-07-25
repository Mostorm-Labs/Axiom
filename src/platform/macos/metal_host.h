#pragma once

#include <cstdint>
#include <memory>

namespace canvas::document {
class Document;
}

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
  void resize(double widthInPoints, double heightInPoints, double backingScale);
  // The host retains the immutable rendering view, while the owner may keep a
  // mutable shared_ptr and update it on the AppKit main thread before calling
  // invalidate(). This avoids copying the document on the pen-preview path.
  void setDocument(std::shared_ptr<const document::Document> document);
  void invalidate();
  // Reassert a pending frame at an AppKit lifecycle boundary even when the
  // earlier setNeedsDisplay request happened before the view entered a window.
  void reschedulePendingFrame();
  void drawIfNeeded();
  std::uint64_t nativeDisplayRequestCount() const noexcept;
  std::uint64_t committedFrameCount() const noexcept;
  bool isReady() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::macos
