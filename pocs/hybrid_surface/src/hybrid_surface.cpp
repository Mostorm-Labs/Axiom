#include "canvas/poc05/hybrid_surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace canvas::poc05 {
namespace {

bool isKnownKind(SurfaceKind kind) {
    return kind == SurfaceKind::kWebView || kind == SurfaceKind::kVideo;
}

bool isFiniteRect(const CanvasRectF& rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height);
}

bool isPositiveRect(const CanvasRectF& rect) {
    return isFiniteRect(rect) && rect.width > 0.0F && rect.height > 0.0F;
}

bool validatePlaceholder(const ExternalSurfacePlaceholder& placeholder,
                         std::string* error) {
    if (error == nullptr) {
        return false;
    }
    if (placeholder.schemaVersion != 1U || placeholder.id == 0U ||
        !isKnownKind(placeholder.kind) ||
        !isPositiveRect(placeholder.worldBounds) ||
        (placeholder.worldClip && !isPositiveRect(*placeholder.worldClip)) ||
        !std::isfinite(placeholder.opacity) || placeholder.opacity < 0.0F ||
        placeholder.opacity > 1.0F || placeholder.pageId == 0U) {
        *error = "invalid ExternalSurface placeholder";
        return false;
    }
    return true;
}

bool validateFrame(const RuntimeViewFrame& frame, std::string* error) {
    if (error == nullptr) {
        return false;
    }
    if (frame.view == CANVAS_INVALID_HANDLE || frame.frameRevision == 0U ||
        frame.camera.struct_size < sizeof(CanvasCameraStateV1) ||
        frame.camera.abi_version != CANVAS_RUNTIME_ABI_VERSION ||
        !std::isfinite(frame.camera.scale) || frame.camera.scale <= 0.0F ||
        frame.camera.viewport_revision == 0U ||
        frame.surface.struct_size < sizeof(CanvasSurfaceStateV1) ||
        frame.surface.abi_version != CANVAS_RUNTIME_ABI_VERSION ||
        frame.surface.width_pixels == 0U ||
        frame.surface.height_pixels == 0U ||
        !std::isfinite(frame.surface.device_pixel_ratio) ||
        frame.surface.device_pixel_ratio <= 0.0F ||
        frame.surface.target_generation == 0U) {
        *error = "invalid Runtime C ABI View frame";
        return false;
    }
    return true;
}

CanvasRectF intersectRects(const CanvasRectF& first, const CanvasRectF& second) {
    const float left = std::max(first.x, second.x);
    const float top = std::max(first.y, second.y);
    const float right = std::min(first.x + first.width,
                                 second.x + second.width);
    const float bottom = std::min(first.y + first.height,
                                  second.y + second.height);
    if (right <= left || bottom <= top) {
        return CanvasRectF{};
    }
    return CanvasRectF{left, top, right - left, bottom - top};
}

CanvasRectF relativeTo(const CanvasRectF& child, const CanvasRectF& parent) {
    if (!isPositiveRect(child)) {
        return CanvasRectF{};
    }
    return CanvasRectF{child.x - parent.x, child.y - parent.y,
                       child.width, child.height};
}

bool projectWorldRect(const CanvasRectF& worldRect,
                      const RuntimeViewFrame& frame,
                      const RuntimeViewProjector& projector,
                      CanvasRectF* deviceRect,
                      std::string* error) {
    if (deviceRect == nullptr || !isPositiveRect(worldRect)) {
        if (error != nullptr) {
            *error = "invalid world projection input";
        }
        return false;
    }
    const std::array<CanvasPointF, 4> worldCorners{
        CanvasPointF{worldRect.x, worldRect.y},
        CanvasPointF{worldRect.x + worldRect.width, worldRect.y},
        CanvasPointF{worldRect.x, worldRect.y + worldRect.height},
        CanvasPointF{worldRect.x + worldRect.width,
                     worldRect.y + worldRect.height},
    };
    std::array<CanvasPointF, 4> logicalCorners{};
    for (std::size_t index = 0; index < worldCorners.size(); ++index) {
        if (!projector.worldToViewLogical(
                frame.view, worldCorners[index], &logicalCorners[index], error)) {
            return false;
        }
        if (!std::isfinite(logicalCorners[index].x) ||
            !std::isfinite(logicalCorners[index].y)) {
            *error = "Runtime C ABI returned non-finite View coordinates";
            return false;
        }
    }
    float left = logicalCorners[0].x;
    float top = logicalCorners[0].y;
    float right = logicalCorners[0].x;
    float bottom = logicalCorners[0].y;
    for (const CanvasPointF point : logicalCorners) {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    const float dpr = frame.surface.device_pixel_ratio;
    *deviceRect = CanvasRectF{left * dpr, top * dpr,
                             (right - left) * dpr,
                             (bottom - top) * dpr};
    return isPositiveRect(*deviceRect);
}

}  // namespace

CAbiRuntimeViewProjector::CAbiRuntimeViewProjector(
    CanvasWorldToScreenFunction function)
    : _function(function) {}

bool CAbiRuntimeViewProjector::worldToViewLogical(
    CanvasViewHandle view,
    CanvasPointF worldPoint,
    CanvasPointF* viewLogicalPoint,
    std::string* error) const {
    if (error == nullptr || viewLogicalPoint == nullptr ||
        _function == nullptr || view == CANVAS_INVALID_HANDLE ||
        !std::isfinite(worldPoint.x) || !std::isfinite(worldPoint.y)) {
        if (error != nullptr) {
            *error = "invalid Runtime C ABI projector call";
        }
        return false;
    }
    const CanvasStatus status = _function(view, worldPoint, viewLogicalPoint);
    if (status != kCanvasStatusOk) {
        *error = "canvas_view_world_to_screen failed with status " +
                 std::to_string(status);
        return false;
    }
    return true;
}

ExternalSurfaceRegistry::ExternalSurfaceRegistry(
    RuntimeViewProjector& projector,
    PlatformOverlayBackend& backend)
    : _projector(projector), _backend(backend) {
    refreshDiagnostics();
}

ExternalSurfaceRegistry::~ExternalSurfaceRegistry() {
    clear();
}

bool ExternalSurfaceRegistry::registerSurface(
    const ExternalSurfacePlaceholder& placeholder,
    std::string* error) {
    if (!validatePlaceholder(placeholder, error)) {
        return false;
    }
    if (_entries.contains(placeholder.id)) {
        *error = "duplicate ExternalSurface ID";
        return false;
    }
    _entries.emplace(placeholder.id, Entry{placeholder});
    refreshDiagnostics();
    return true;
}

bool ExternalSurfaceRegistry::updateSurface(
    const ExternalSurfacePlaceholder& placeholder,
    std::string* error) {
    if (!validatePlaceholder(placeholder, error)) {
        return false;
    }
    auto found = _entries.find(placeholder.id);
    if (found == _entries.end()) {
        *error = "missing ExternalSurface ID";
        return false;
    }
    if (found->second.placeholder.kind != placeholder.kind) {
        *error = "ExternalSurface kind is immutable";
        return false;
    }
    found->second.placeholder = placeholder;
    refreshDiagnostics();
    return true;
}

bool ExternalSurfaceRegistry::unregisterSurface(ExternalSurfaceId id,
                                                std::string* error) {
    if (error == nullptr) {
        return false;
    }
    auto found = _entries.find(id);
    if (found == _entries.end()) {
        *error = "missing ExternalSurface ID";
        return false;
    }
    if (found->second.materialized) {
        _backend.destroy(id);
        ++_diagnostics.destroyCount;
    }
    if (_focusedSurface == id) {
        _focusedSurface.reset();
        _backend.focusCanvas();
        ++_diagnostics.focusHandoffCount;
    }
    _entries.erase(found);
    refreshDiagnostics();
    return true;
}

void ExternalSurfaceRegistry::clear() {
    resetMaterialized();
    _entries.clear();
    _focusedSurface.reset();
    refreshDiagnostics();
}

bool ExternalSurfaceRegistry::setHidden(ExternalSurfaceId id,
                                        bool hidden,
                                        std::string* error) {
    if (error == nullptr) {
        return false;
    }
    auto found = _entries.find(id);
    if (found == _entries.end()) {
        *error = "missing ExternalSurface ID";
        return false;
    }
    found->second.hidden = hidden;
    return true;
}

bool ExternalSurfaceRegistry::setContentState(ExternalSurfaceId id,
                                              SurfaceContentState state,
                                              std::string* error) {
    if (error == nullptr) {
        return false;
    }
    auto found = _entries.find(id);
    if (found == _entries.end()) {
        *error = "missing ExternalSurface ID";
        return false;
    }
    found->second.contentState = state;
    return true;
}

bool ExternalSurfaceRegistry::markReady(ExternalSurfaceId id,
                                        std::string* error) {
    return setContentState(id, SurfaceContentState::kReady, error);
}

bool ExternalSurfaceRegistry::markFailed(ExternalSurfaceId id,
                                         std::string* error) {
    return setContentState(id, SurfaceContentState::kFailed, error);
}

bool ExternalSurfaceRegistry::recover(ExternalSurfaceId id,
                                      std::string* error) {
    return setContentState(id, SurfaceContentState::kLoading, error);
}

void ExternalSurfaceRegistry::setActivePage(std::uint64_t pageId) {
    if (pageId != 0U) {
        _activePageId = pageId;
    }
}

void ExternalSurfaceRegistry::setBackgrounded(bool backgrounded) {
    _backgrounded = backgrounded;
    if (_backgrounded && _focusedSurface) {
        focusCanvas();
    }
}

bool ExternalSurfaceRegistry::focusExternal(ExternalSurfaceId id,
                                            std::string* error) {
    if (error == nullptr) {
        return false;
    }
    const auto found = _entries.find(id);
    if (found == _entries.end() || !found->second.materialized ||
        found->second.hidden ||
        found->second.placeholder.pageId != _activePageId) {
        *error = "ExternalSurface is not focusable";
        return false;
    }
    if (_focusedSurface == id) {
        return true;
    }
    if (!_backend.focus(id, error)) {
        ++_diagnostics.backendFailureCount;
        return false;
    }
    _focusedSurface = id;
    ++_diagnostics.focusHandoffCount;
    return true;
}

void ExternalSurfaceRegistry::focusCanvas() {
    if (!_focusedSurface) {
        return;
    }
    _focusedSurface.reset();
    _backend.focusCanvas();
    ++_diagnostics.focusHandoffCount;
}

bool ExternalSurfaceRegistry::applyFrame(const RuntimeViewFrame& frame,
                                         std::string* error) {
    if (!validateFrame(frame, error)) {
        ++_diagnostics.invalidFrameCount;
        return false;
    }
    const std::uint32_t generation = frame.surface.target_generation;
    const std::uint64_t viewportRevision = frame.camera.viewport_revision;
    if (_targetGeneration != 0U &&
        (generation < _targetGeneration ||
         (generation == _targetGeneration &&
          (frame.frameRevision < _lastFrameRevision ||
           viewportRevision < _lastViewportRevision)))) {
        ++_diagnostics.staleFrameCount;
        return true;
    }
    if (_targetGeneration != generation) {
        resetMaterialized();
        _targetGeneration = generation;
        _lastFrameRevision = 0U;
        _lastViewportRevision = 0U;
    }

    std::vector<Entry*> ordered;
    ordered.reserve(_entries.size());
    for (auto& [id, entry] : _entries) {
        static_cast<void>(id);
        ordered.push_back(&entry);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const Entry* left, const Entry* right) {
                  return left->placeholder.order < right->placeholder.order ||
                         (left->placeholder.order == right->placeholder.order &&
                          left->placeholder.id < right->placeholder.id);
              });

    const CanvasRectF deviceViewport{
        0.0F, 0.0F, static_cast<float>(frame.surface.width_pixels),
        static_cast<float>(frame.surface.height_pixels)};
    for (Entry* entry : ordered) {
        if (!entry->materialized) {
            if (!_backend.create(entry->placeholder.id,
                                 entry->placeholder.kind, error)) {
                ++_diagnostics.backendFailureCount;
                return false;
            }
            entry->materialized = true;
            ++_diagnostics.createCount;
        }

        CanvasRectF deviceBounds{};
        if (!projectWorldRect(entry->placeholder.worldBounds, frame,
                              _projector, &deviceBounds, error)) {
            ++_diagnostics.invalidFrameCount;
            return false;
        }
        CanvasRectF deviceClip = intersectRects(deviceBounds, deviceViewport);
        if (entry->placeholder.worldClip) {
            CanvasRectF projectedClip{};
            if (!projectWorldRect(*entry->placeholder.worldClip, frame,
                                  _projector, &projectedClip, error)) {
                ++_diagnostics.invalidFrameCount;
                return false;
            }
            deviceClip = intersectRects(deviceClip, projectedClip);
        }

        const bool hasClip = isPositiveRect(deviceClip);
        const auto& placeholder = entry->placeholder;
        const bool visible = !_backgrounded && !entry->hidden && hasClip &&
                             placeholder.pageId == _activePageId &&
                             placeholder.opacity > 0.0F;
        PlacementCommand command;
        command.id = placeholder.id;
        command.kind = placeholder.kind;
        command.frameRevision = frame.frameRevision;
        command.viewportRevision = viewportRevision;
        command.targetGeneration = generation;
        command.deviceBounds = deviceBounds;
        command.relativeDeviceClip = relativeTo(deviceClip, deviceBounds);
        command.opacity = placeholder.opacity;
        command.order = placeholder.order;
        command.visible = visible;
        command.contentVisible =
            visible && entry->contentState == SurfaceContentState::kReady;
        command.failurePlaceholder =
            visible && entry->contentState == SurfaceContentState::kFailed;
        if (!_backend.apply(command, error)) {
            ++_diagnostics.backendFailureCount;
            return false;
        }
        ++_diagnostics.placementCount;
    }

    _lastFrameRevision = frame.frameRevision;
    _lastViewportRevision = viewportRevision;
    refreshDiagnostics();
    return true;
}

void ExternalSurfaceRegistry::resetMaterialized() {
    for (auto& [id, entry] : _entries) {
        if (entry.materialized) {
            _backend.destroy(id);
            ++_diagnostics.destroyCount;
            entry.materialized = false;
        }
    }
    if (_focusedSurface) {
        _focusedSurface.reset();
        _backend.focusCanvas();
        ++_diagnostics.focusHandoffCount;
    }
}

void ExternalSurfaceRegistry::refreshDiagnostics() {
    _diagnostics.activeSurfaceCount = _entries.size();
    _diagnostics.materializedSurfaceCount = static_cast<std::size_t>(
        std::count_if(_entries.begin(), _entries.end(),
                      [](const auto& pair) {
                          return pair.second.materialized;
                      }));
    _diagnostics.retainedSemanticBytes =
        _entries.size() * (sizeof(ExternalSurfaceId) + sizeof(Entry));
}

}  // namespace canvas::poc05
