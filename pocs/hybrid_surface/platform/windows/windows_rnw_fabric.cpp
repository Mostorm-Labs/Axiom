#include "windows_rnw_fabric.h"

#include <utility>

namespace canvas::poc05::windows {

WindowsRnwFabricExternalSurfaceHost::WindowsRnwFabricExternalSurfaceHost(
    CanvasWorldToScreenFunction world_to_screen,
    PlatformOverlayBackend& backend)
    : projector_(world_to_screen), registry_(projector_, backend) {}

bool WindowsRnwFabricExternalSurfaceHost::mount(
    std::uint64_t surface_id, SurfaceKind kind, const CanvasRectF& world_bounds,
    std::uint64_t page_id, std::string* error) {
  ExternalSurfacePlaceholder placeholder;
  placeholder.id = surface_id;
  placeholder.kind = kind;
  placeholder.worldBounds = world_bounds;
  placeholder.pageId = page_id;
  placeholder.order = next_order_++;
  if (!registry_.registerSurface(placeholder, error)) return false;
  placeholders_.emplace(surface_id, placeholder);
  return true;
}

bool WindowsRnwFabricExternalSurfaceHost::update(
    std::uint64_t surface_id, const CanvasRectF& world_bounds, float opacity,
    bool hidden, std::string* error) {
  auto found = placeholders_.find(surface_id);
  if (found == placeholders_.end()) {
    if (error) *error = "missing Fabric ExternalSurface ID";
    return false;
  }
  found->second.worldBounds = world_bounds;
  found->second.opacity = opacity;
  if (!registry_.updateSurface(found->second, error)) return false;
  return registry_.setHidden(surface_id, hidden, error);
}

bool WindowsRnwFabricExternalSurfaceHost::unmount(std::uint64_t surface_id,
                                                  std::string* error) {
  if (!registry_.unregisterSurface(surface_id, error)) return false;
  placeholders_.erase(surface_id);
  return true;
}

bool WindowsRnwFabricExternalSurfaceHost::setReady(std::uint64_t surface_id,
                                                   std::string* error) {
  return registry_.markReady(surface_id, error);
}

bool WindowsRnwFabricExternalSurfaceHost::setFailed(std::uint64_t surface_id,
                                                    std::string* error) {
  return registry_.markFailed(surface_id, error);
}

bool WindowsRnwFabricExternalSurfaceHost::recover(std::uint64_t surface_id,
                                                  std::string* error) {
  return registry_.recover(surface_id, error);
}

void WindowsRnwFabricExternalSurfaceHost::setActivePage(std::uint64_t page_id) {
  registry_.setActivePage(page_id);
}

void WindowsRnwFabricExternalSurfaceHost::setBackgrounded(bool backgrounded) {
  registry_.setBackgrounded(backgrounded);
}

bool WindowsRnwFabricExternalSurfaceHost::publishFrame(
    const RuntimeViewFrame& frame, std::string* error) {
  return registry_.applyFrame(frame, error);
}

bool WindowsRnwFabricExternalSurfaceHost::focus(std::uint64_t surface_id,
                                                std::string* error) {
  return registry_.focusExternal(surface_id, error);
}

void WindowsRnwFabricExternalSurfaceHost::focusCanvas() {
  registry_.focusCanvas();
}

const RegistryDiagnostics& WindowsRnwFabricExternalSurfaceHost::diagnostics()
    const {
  return registry_.diagnostics();
}

}  // namespace canvas::poc05::windows
