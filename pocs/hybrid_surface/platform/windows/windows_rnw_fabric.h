#ifndef CANVAS_POC05_WINDOWS_RNW_FABRIC_H_
#define CANVAS_POC05_WINDOWS_RNW_FABRIC_H_

#include <cstdint>
#include <string>
#include <unordered_map>

#include "canvas/poc05/hybrid_surface.h"

namespace canvas::poc05::windows {

// This is the native peer boundary for an RNW Fabric ExternalSurfaceHost.
// An actual RNW component forwards low-frequency Fabric props here; it never
// receives per-frame placement.  Keeping this class free of RNW headers makes
// the adapter testable in a standalone physical runner and allows a product
// shell to provide the RNW view manager without changing the POC contract.
class WindowsRnwFabricExternalSurfaceHost final {
 public:
  WindowsRnwFabricExternalSurfaceHost(
      CanvasWorldToScreenFunction world_to_screen,
      PlatformOverlayBackend& backend);
  ~WindowsRnwFabricExternalSurfaceHost() = default;
  WindowsRnwFabricExternalSurfaceHost(
      const WindowsRnwFabricExternalSurfaceHost&) = delete;
  WindowsRnwFabricExternalSurfaceHost& operator=(
      const WindowsRnwFabricExternalSurfaceHost&) = delete;

  bool mount(std::uint64_t surface_id, SurfaceKind kind,
             const CanvasRectF& world_bounds, std::uint64_t page_id,
             std::string* error);
  bool update(std::uint64_t surface_id, const CanvasRectF& world_bounds,
              float opacity, bool hidden, std::string* error);
  bool unmount(std::uint64_t surface_id, std::string* error);
  bool setReady(std::uint64_t surface_id, std::string* error);
  bool setFailed(std::uint64_t surface_id, std::string* error);
  bool recover(std::uint64_t surface_id, std::string* error);
  void setActivePage(std::uint64_t page_id);
  void setBackgrounded(bool backgrounded);
  bool publishFrame(const RuntimeViewFrame& frame, std::string* error);
  bool focus(std::uint64_t surface_id, std::string* error);
  void focusCanvas();

  [[nodiscard]] const RegistryDiagnostics& diagnostics() const;

 private:
  CAbiRuntimeViewProjector projector_;
  ExternalSurfaceRegistry registry_;
  std::unordered_map<std::uint64_t, ExternalSurfacePlaceholder> placeholders_;
  std::uint32_t next_order_ = 1;
};

}  // namespace canvas::poc05::windows

#endif  // CANVAS_POC05_WINDOWS_RNW_FABRIC_H_
