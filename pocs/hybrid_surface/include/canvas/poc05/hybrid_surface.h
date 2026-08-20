#ifndef CANVAS_POC05_HYBRID_SURFACE_H_
#define CANVAS_POC05_HYBRID_SURFACE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "canvas_runtime_api_v1.h"

namespace canvas::poc05 {

using ExternalSurfaceId = std::uint64_t;

enum class SurfaceKind : std::uint8_t { kWebView = 1, kVideo = 2 };
enum class SurfaceContentState : std::uint8_t {
    kLoading = 1,
    kReady = 2,
    kFailed = 3,
};

// Experimental future-capability data. It is not a V1 Document node and does
// not extend the stable Runtime C ABI.
struct ExternalSurfacePlaceholder {
    std::uint32_t schemaVersion = 1;
    ExternalSurfaceId id = 0;
    SurfaceKind kind = SurfaceKind::kWebView;
    CanvasRectF worldBounds{};
    std::optional<CanvasRectF> worldClip;
    float opacity = 1.0F;
    std::uint32_t order = 0;
    std::uint64_t pageId = 1;
};

enum class OverlayLayer : std::uint8_t {
    kAboveCanvasBelowProductUi = 1,
};

// Assembled on the Runtime owner thread from stable C ABI state. No Scene or
// native platform handle is exposed to this experiment.
struct RuntimeViewFrame {
    CanvasViewHandle view = CANVAS_INVALID_HANDLE;
    CanvasCameraStateV1 camera{};
    CanvasSurfaceStateV1 surface{};
    std::uint64_t frameRevision = 0;
};

struct PlacementCommand {
    ExternalSurfaceId id = 0;
    SurfaceKind kind = SurfaceKind::kWebView;
    std::uint64_t frameRevision = 0;
    std::uint64_t viewportRevision = 0;
    std::uint32_t targetGeneration = 0;
    CanvasRectF deviceBounds{};
    CanvasRectF relativeDeviceClip{};
    float opacity = 1.0F;
    std::uint32_t order = 0;
    OverlayLayer layer = OverlayLayer::kAboveCanvasBelowProductUi;
    bool visible = false;
    bool contentVisible = false;
    bool failurePlaceholder = false;
};

class RuntimeViewProjector {
public:
    virtual ~RuntimeViewProjector() = default;
    virtual bool worldToViewLogical(
        CanvasViewHandle view,
        CanvasPointF worldPoint,
        CanvasPointF* viewLogicalPoint,
        std::string* error) const = 0;
};

using CanvasWorldToScreenFunction = CanvasStatus (*)(
    CanvasViewHandle view,
    CanvasPointF worldPoint,
    CanvasPointF* viewLogicalPoint);

class CAbiRuntimeViewProjector final : public RuntimeViewProjector {
public:
    explicit CAbiRuntimeViewProjector(CanvasWorldToScreenFunction function);
    bool worldToViewLogical(
        CanvasViewHandle view,
        CanvasPointF worldPoint,
        CanvasPointF* viewLogicalPoint,
        std::string* error) const override;

private:
    CanvasWorldToScreenFunction _function = nullptr;
};

// DOM, WebView2, Android Views and Apple UIViews live only behind this
// interface. RN/Fabric owns the declarative Shell and its native component
// owns the platform handles.
class PlatformOverlayBackend {
public:
    virtual ~PlatformOverlayBackend() = default;
    virtual bool create(ExternalSurfaceId id, SurfaceKind kind,
                        std::string* error) = 0;
    virtual bool apply(const PlacementCommand& command,
                       std::string* error) = 0;
    virtual void destroy(ExternalSurfaceId id) = 0;
    virtual bool focus(ExternalSurfaceId id, std::string* error) = 0;
    virtual void focusCanvas() = 0;
};

struct RegistryDiagnostics {
    std::uint64_t createCount = 0;
    std::uint64_t destroyCount = 0;
    std::uint64_t placementCount = 0;
    std::uint64_t staleFrameCount = 0;
    std::uint64_t invalidFrameCount = 0;
    std::uint64_t backendFailureCount = 0;
    std::uint64_t focusHandoffCount = 0;
    std::size_t activeSurfaceCount = 0;
    std::size_t materializedSurfaceCount = 0;
    std::size_t retainedSemanticBytes = 0;
};

class ExternalSurfaceRegistry final {
public:
    ExternalSurfaceRegistry(RuntimeViewProjector& projector,
                            PlatformOverlayBackend& backend);
    ~ExternalSurfaceRegistry();
    ExternalSurfaceRegistry(const ExternalSurfaceRegistry&) = delete;
    ExternalSurfaceRegistry& operator=(const ExternalSurfaceRegistry&) = delete;

    bool registerSurface(const ExternalSurfacePlaceholder& placeholder,
                         std::string* error);
    bool updateSurface(const ExternalSurfacePlaceholder& placeholder,
                       std::string* error);
    bool unregisterSurface(ExternalSurfaceId id, std::string* error);
    void clear();
    bool setHidden(ExternalSurfaceId id, bool hidden, std::string* error);
    bool markReady(ExternalSurfaceId id, std::string* error);
    bool markFailed(ExternalSurfaceId id, std::string* error);
    bool recover(ExternalSurfaceId id, std::string* error);
    void setActivePage(std::uint64_t pageId);
    void setBackgrounded(bool backgrounded);
    bool focusExternal(ExternalSurfaceId id, std::string* error);
    void focusCanvas();

    // Must run on the same single-owner thread as Runtime View calls.
    bool applyFrame(const RuntimeViewFrame& frame, std::string* error);

    [[nodiscard]] const RegistryDiagnostics& diagnostics() const {
        return _diagnostics;
    }
    [[nodiscard]] std::optional<ExternalSurfaceId> focusedSurface() const {
        return _focusedSurface;
    }

private:
    struct Entry {
        ExternalSurfacePlaceholder placeholder;
        SurfaceContentState contentState = SurfaceContentState::kLoading;
        bool hidden = false;
        bool materialized = false;
    };
    bool setContentState(ExternalSurfaceId id, SurfaceContentState state,
                         std::string* error);
    void refreshDiagnostics();
    void resetMaterialized();

    RuntimeViewProjector& _projector;
    PlatformOverlayBackend& _backend;
    std::unordered_map<ExternalSurfaceId, Entry> _entries;
    std::uint64_t _activePageId = 1;
    std::uint64_t _lastViewportRevision = 0;
    std::uint64_t _lastFrameRevision = 0;
    std::uint32_t _targetGeneration = 0;
    bool _backgrounded = false;
    std::optional<ExternalSurfaceId> _focusedSurface;
    RegistryDiagnostics _diagnostics;
};

}  // namespace canvas::poc05

#endif  // CANVAS_POC05_HYBRID_SURFACE_H_
