#pragma once

#include <stdint.h>

#if defined(_WIN32) && defined(CANVAS_RUNTIME_SHARED_LIBRARY)
#if defined(CANVAS_RUNTIME_IMPLEMENTATION)
#define CANVAS_RUNTIME_API __declspec(dllexport)
#else
#define CANVAS_RUNTIME_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(CANVAS_RUNTIME_SHARED_LIBRARY)
#define CANVAS_RUNTIME_API __attribute__((visibility("default")))
#else
#define CANVAS_RUNTIME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CANVAS_RUNTIME_ABI_VERSION 1u
#define CANVAS_INVALID_HANDLE 0u

typedef uint32_t CanvasRuntimeHandle;
typedef uint32_t CanvasDocumentHandle;
typedef uint32_t CanvasViewHandle;
typedef uint32_t CanvasSurfaceHandle;

typedef struct CanvasByteSpan {
    const uint8_t* data;
    uint64_t size;
} CanvasByteSpan;

typedef struct CanvasMutableByteBuffer {
    uint8_t* data;
    uint64_t capacity;
} CanvasMutableByteBuffer;

typedef struct CanvasStringView {
    const char* data;
    uint64_t size;
} CanvasStringView;

typedef struct CanvasDocumentId {
    uint8_t bytes[16];
} CanvasDocumentId;

typedef struct CanvasOperationId {
    uint8_t bytes[16];
} CanvasOperationId;

typedef struct CanvasActorId {
    uint8_t bytes[16];
} CanvasActorId;

typedef struct CanvasClientId {
    uint8_t bytes[16];
} CanvasClientId;

typedef struct CanvasObjectId {
    uint8_t bytes[16];
} CanvasObjectId;

typedef struct CanvasResourceId {
    uint8_t bytes[16];
} CanvasResourceId;

typedef struct CanvasBrushId {
    uint8_t bytes[16];
} CanvasBrushId;

typedef struct CanvasHash256 {
    uint8_t bytes[32];
} CanvasHash256;

typedef struct CanvasPointF {
    float x;
    float y;
} CanvasPointF;

typedef struct CanvasSizeF {
    float width;
    float height;
} CanvasSizeF;

typedef struct CanvasRectF {
    float x;
    float y;
    float width;
    float height;
} CanvasRectF;

typedef struct CanvasColorRgba8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} CanvasColorRgba8;

typedef uint32_t CanvasStatus;
enum {
    kCanvasStatusOk = 0,
    kCanvasStatusInvalidArgument = 1,
    kCanvasStatusAbiMismatch = 2,
    kCanvasStatusInvalidHandle = 3,
    kCanvasStatusWrongHandleType = 4,
    kCanvasStatusNotSupported = 5,
    kCanvasStatusUnavailable = 6,
    kCanvasStatusBufferTooSmall = 7,
    kCanvasStatusInvalidState = 8,
    kCanvasStatusParseError = 9,
    kCanvasStatusValidationError = 10,
    kCanvasStatusSequenceError = 11,
    kCanvasStatusResourcePending = 12,
    kCanvasStatusResourceMissing = 13,
    kCanvasStatusPlatformError = 14,
    kCanvasStatusCancelled = 15,
    kCanvasStatusInputOverrun = 16,
    kCanvasStatusInternalError = 17
};

typedef uint32_t CanvasRendererBackend;
enum {
    kCanvasRendererBackendAuto = 0,
    kCanvasRendererBackendD3D12 = 1,
    kCanvasRendererBackendWebGl2 = 2,
    kCanvasRendererBackendMetal = 3,
    kCanvasRendererBackendGles3 = 4,
    kCanvasRendererBackendRaster = 5
};

typedef uint64_t CanvasRendererCapabilities;
enum {
    kCanvasRendererCapabilityGanesh = 1ull << 0,
    kCanvasRendererCapabilityD3D12 = 1ull << 1,
    kCanvasRendererCapabilityWebGl2 = 1ull << 2,
    kCanvasRendererCapabilityMetal = 1ull << 3,
    kCanvasRendererCapabilityGles3 = 1ull << 4,
    kCanvasRendererCapabilityFastInk = 1ull << 5
};

typedef uint64_t CanvasInputCapabilities;
enum {
    kCanvasInputCapabilityPressure = 1ull << 0,
    kCanvasInputCapabilityTilt = 1ull << 1,
    kCanvasInputCapabilityTwist = 1ull << 2,
    kCanvasInputCapabilityCoalesced = 1ull << 3,
    kCanvasInputCapabilityPredicted = 1ull << 4,
    kCanvasInputCapabilityImeComposition = 1ull << 5
};

typedef uint64_t CanvasPlatformCapabilities;
enum {
    kCanvasPlatformCapabilityClipboard = 1ull << 0,
    kCanvasPlatformCapabilityAccessibility = 1ull << 1,
    kCanvasPlatformCapabilityExternalSurface = 1ull << 2,
    kCanvasPlatformCapabilityPersistentResources = 1ull << 3
};

typedef uint32_t CanvasLogLevel;
enum {
    kCanvasLogDebug = 0,
    kCanvasLogInfo = 1,
    kCanvasLogWarning = 2,
    kCanvasLogError = 3
};

typedef uint32_t CanvasFrameDecision;
enum {
    kCanvasFrameNoFrameNeeded = 0,
    kCanvasFrameNeeded = 1,
    kCanvasFrameUrgent = 2
};

typedef uint32_t CanvasColorSpace;
enum {
    kCanvasColorSpaceSrgb = 1,
    kCanvasColorSpaceDisplayP3 = 2,
    kCanvasColorSpaceLinearSrgb = 3
};

typedef uint32_t CanvasSurfaceOrientation;
enum {
    kCanvasSurfaceOrientationIdentity = 0,
    kCanvasSurfaceOrientationRotate90 = 1,
    kCanvasSurfaceOrientationRotate180 = 2,
    kCanvasSurfaceOrientationRotate270 = 3
};

typedef uint32_t CanvasOperationDurability;
enum {
    kCanvasOperationAppliedLocally = 1u << 0,
    kCanvasOperationPersistedLocally = 1u << 1,
    kCanvasOperationQueuedForSync = 1u << 2,
    kCanvasOperationServerAcknowledged = 1u << 3
};

typedef uint32_t CanvasPointerDevice;
enum {
    kCanvasPointerMouse = 1,
    kCanvasPointerTouch = 2,
    kCanvasPointerPen = 3,
    kCanvasPointerEraser = 4
};

typedef uint32_t CanvasPointerPhase;
enum {
    kCanvasPointerBegin = 1,
    kCanvasPointerMove = 2,
    kCanvasPointerEnd = 3,
    kCanvasPointerCancel = 4
};

typedef uint32_t CanvasPointerSampleFlags;
enum {
    kCanvasPointerConfirmed = 1u << 0,
    kCanvasPointerPredicted = 1u << 1,
    kCanvasPointerCoalesced = 1u << 2,
    kCanvasPointerPrimary = 1u << 3,
    kCanvasPointerBarrel = 1u << 4
};

typedef uint32_t CanvasWheelEventFlags;
enum {
    kCanvasWheelDeltaPixels = 1u << 0,
    kCanvasWheelDeltaLines = 1u << 1,
    kCanvasWheelPrecise = 1u << 2,
    kCanvasWheelInverted = 1u << 3
};

typedef uint32_t CanvasKeyModifiers;
enum {
    kCanvasKeyModifierShift = 1u << 0,
    kCanvasKeyModifierControl = 1u << 1,
    kCanvasKeyModifierAlt = 1u << 2,
    kCanvasKeyModifierMeta = 1u << 3,
    kCanvasKeyModifierCapsLock = 1u << 4,
    kCanvasKeyModifierNumLock = 1u << 5
};

typedef uint32_t CanvasToolType;
enum {
    kCanvasToolPen = 1,
    kCanvasToolBrush = 2,
    kCanvasToolMarker = 3,
    kCanvasToolHighlighter = 4,
    kCanvasToolEraser = 5,
    kCanvasToolSelect = 6,
    kCanvasToolLasso = 7,
    kCanvasToolPan = 8,
    kCanvasToolShape = 9,
    kCanvasToolText = 10,
    kCanvasToolLaserPointer = 11
};

typedef uint32_t CanvasEraserMode;
enum {
    kCanvasEraserObject = 1,
    kCanvasEraserStroke = 2,
    kCanvasEraserPartialVector = 3,
    kCanvasEraserObjectMask = 4,
    kCanvasEraserRaster = 5
};

typedef uint32_t CanvasBrushTip;
enum {
    kCanvasBrushTipRoundVector = 1,
    kCanvasBrushTipRoundDab = 2,
    kCanvasBrushTipTextureDab = 3
};

typedef uint32_t CanvasBlendMode;
enum {
    kCanvasBlendModeSourceOver = 1,
    kCanvasBlendModeMultiply = 2
};

typedef uint32_t CanvasEraserScope;
enum {
    kCanvasEraserScopeCurrentLayer = 1,
    kCanvasEraserScopeVisibleLayers = 2,
    kCanvasEraserScopeAllLayers = 3
};

typedef uint32_t CanvasCommandType;
enum {
    kCanvasCommandDeleteSelection = 1,
    kCanvasCommandSelectAll = 2,
    kCanvasCommandClearSelection = 3,
    kCanvasCommandGroupSelection = 4,
    kCanvasCommandUngroupSelection = 5,
    kCanvasCommandBringForward = 6,
    kCanvasCommandSendBackward = 7,
    kCanvasCommandInsertShape = 8,
    kCanvasCommandInsertImage = 9,
    kCanvasCommandInsertText = 10,
    kCanvasCommandMoveSelection = 11,
    kCanvasCommandTransformSelection = 12,
    kCanvasCommandCopySelection = 13,
    kCanvasCommandCutSelection = 14,
    kCanvasCommandPaste = 15
};

typedef uint32_t CanvasKeyPhase;
enum {
    kCanvasKeyDown = 1,
    kCanvasKeyUp = 2,
    kCanvasKeyRepeat = 3
};

typedef uint32_t CanvasImeEventType;
enum {
    kCanvasImeBeginComposition = 1,
    kCanvasImeUpdateComposition = 2,
    kCanvasImeCommitComposition = 3,
    kCanvasImeCancelComposition = 4,
    kCanvasImeSetSelection = 5
};

typedef uint32_t CanvasObjectType;
enum {
    kCanvasObjectTypeNone = 0,
    kCanvasObjectTypeMixed = 1,
    kCanvasObjectTypeShape = 2,
    kCanvasObjectTypeImage = 3,
    kCanvasObjectTypeVectorPath = 4,
    kCanvasObjectTypeRichText = 5,
    kCanvasObjectTypeVectorStroke = 6,
    kCanvasObjectTypeDabStroke = 7
};

typedef uint64_t CanvasSelectionCapabilities;
enum {
    kCanvasSelectionCanMove = 1ull << 0,
    kCanvasSelectionCanResize = 1ull << 1,
    kCanvasSelectionCanRotate = 1ull << 2,
    kCanvasSelectionCanDelete = 1ull << 3,
    kCanvasSelectionCanGroup = 1ull << 4,
    kCanvasSelectionCanEditStyle = 1ull << 5,
    kCanvasSelectionCanEditText = 1ull << 6
};

typedef uint32_t CanvasResourcePurpose;
enum {
    kCanvasResourcePurposeImage = 1,
    kCanvasResourcePurposeFont = 2,
    kCanvasResourcePurposeBrushTexture = 3
};

typedef uint32_t CanvasEventType;
enum {
    kCanvasEventLocalOperationCommitted = 1,
    kCanvasEventDocumentChanged = 2,
    kCanvasEventSelectionChanged = 3,
    kCanvasEventToolStateChanged = 4,
    kCanvasEventCameraChanged = 5,
    kCanvasEventResourceRequested = 6,
    kCanvasEventSnapshotRecommended = 7,
    kCanvasEventError = 8,
    kCanvasEventPerformanceWarning = 9,
    kCanvasEventOperationDurabilityChanged = 10,
    kCanvasEventLocalPresenceChanged = 11,
    kCanvasEventTextInputStateChanged = 12
};

typedef uint32_t CanvasDebugOverlayFlags;
enum {
    kCanvasDebugNone = 0,
    kCanvasDebugTileBounds = 1u << 0,
    kCanvasDebugDamageRegions = 1u << 1,
    kCanvasDebugFps = 1u << 2,
    kCanvasDebugSceneStats = 1u << 3
};

typedef struct CanvasEventHeaderV1 CanvasEventHeaderV1;

typedef void (*CanvasLogCallback)(void* user_data, CanvasLogLevel level,
                                  CanvasStringView message);
typedef void (*CanvasRequestFrameCallback)(void* user_data, CanvasViewHandle view,
                                           uint64_t revision,
                                           uint32_t target_generation);
typedef void (*CanvasEventCallback)(void* user_data, const CanvasEventHeaderV1* event);

typedef struct CanvasRuntimeCallbacksV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* user_data;
    CanvasLogCallback log;
    CanvasRequestFrameCallback request_frame;
    CanvasEventCallback event;
} CanvasRuntimeCallbacksV1;

typedef struct CanvasRuntimeConfigV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const CanvasRuntimeCallbacksV1* callbacks;
    CanvasRendererBackend requested_backend;
    uint32_t flags;
    uint64_t gpu_cache_budget_bytes;
    uint64_t tile_cache_budget_bytes;
    uint64_t resource_budget_bytes;
} CanvasRuntimeConfigV1;

typedef struct CanvasCapabilitiesV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasRendererCapabilities renderer_flags;
    CanvasInputCapabilities input_flags;
    CanvasPlatformCapabilities platform_flags;
    CanvasRendererBackend active_backend;
    uint64_t maximum_pointer_batch_samples;
    uint64_t maximum_views_per_document;
} CanvasCapabilitiesV1;

typedef struct CanvasEncodedDataV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasStringView codec_id;
    CanvasByteSpan bytes;
} CanvasEncodedDataV1;

typedef struct CanvasOperationEnvelopeV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasOperationId operation_id;
    CanvasDocumentId document_id;
    CanvasActorId actor_id;
    CanvasClientId client_id;
    uint64_t client_sequence;
    uint64_t logical_time;
    uint32_t schema_version;
    uint32_t flags;
    CanvasStringView payload_codec_id;
    CanvasByteSpan payload;
} CanvasOperationEnvelopeV1;

typedef struct CanvasOperationBatchV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const CanvasOperationEnvelopeV1* operations;
    uint64_t operation_count;
    uint64_t operation_stride;
} CanvasOperationBatchV1;

typedef struct CanvasDocumentOpenInfoV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasDocumentId document_id;
    CanvasActorId local_actor_id;
    CanvasClientId local_client_id;
    uint32_t flags;
    const CanvasEncodedDataV1* snapshot;
    const CanvasEncodedDataV1* operation_continuation;
} CanvasDocumentOpenInfoV1;

typedef struct CanvasSnapshotWriteOptionsV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasStringView codec_id;
    uint32_t flags;
} CanvasSnapshotWriteOptionsV1;

typedef struct CanvasDocumentStateV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasDocumentId document_id;
    uint64_t revision;
    uint64_t local_operation_count;
    uint64_t pending_resource_count;
} CanvasDocumentStateV1;

typedef struct CanvasViewConfigV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t flags;
    float width_logical;
    float height_logical;
    float device_pixel_ratio;
} CanvasViewConfigV1;

typedef struct CanvasViewportV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    float width_logical;
    float height_logical;
    float device_pixel_ratio;
} CanvasViewportV1;

typedef struct CanvasCameraV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    float scale;
    float world_origin_x;
    float world_origin_y;
} CanvasCameraV1;

typedef struct CanvasCameraStateV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    float scale;
    float world_origin_x;
    float world_origin_y;
    uint64_t viewport_revision;
} CanvasCameraStateV1;

typedef struct CanvasSurfaceStateV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t width_pixels;
    uint32_t height_pixels;
    float device_pixel_ratio;
    uint32_t target_generation;
    CanvasColorSpace color_space;
    CanvasSurfaceOrientation orientation;
} CanvasSurfaceStateV1;

typedef struct CanvasPointerSampleV1 {
    uint64_t pointer_id;
    CanvasPointerDevice device;
    CanvasPointerPhase phase;
    CanvasPointerSampleFlags flags;
    float x;
    float y;
    float pressure;
    float tilt_x;
    float tilt_y;
    float twist;
    float contact_width;
    float contact_height;
    uint32_t buttons;
    uint64_t timestamp_ns;
} CanvasPointerSampleV1;

typedef struct CanvasPointerBatchV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const CanvasPointerSampleV1* samples;
    uint64_t sample_count;
    uint64_t sample_stride;
    uint64_t viewport_revision;
} CanvasPointerBatchV1;

typedef struct CanvasWheelEventV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    float x;
    float y;
    float delta_x;
    float delta_y;
    CanvasWheelEventFlags flags;
    uint64_t timestamp_ns;
} CanvasWheelEventV1;

typedef struct CanvasKeyEventV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t physical_key;
    uint32_t logical_key;
    CanvasKeyModifiers modifiers;
    CanvasKeyPhase phase;
    uint64_t timestamp_ns;
} CanvasKeyEventV1;

typedef struct CanvasTextRangeV1 {
    uint64_t start_scalar;
    uint64_t length_scalars;
} CanvasTextRangeV1;

typedef struct CanvasImeEventV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasImeEventType type;
    uint32_t flags;
    CanvasStringView text;
    CanvasTextRangeV1 replacement_range;
    CanvasTextRangeV1 selection_range;
    uint64_t surrounding_text_revision;
    uint64_t timestamp_ns;
} CanvasImeEventV1;

typedef struct CanvasTextInputStateV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t active;
    uint32_t flags;
    CanvasTextRangeV1 selection_range;
    CanvasTextRangeV1 composition_range;
    CanvasRectF caret_rect_view_logical;
    uint64_t surrounding_text_revision;
} CanvasTextInputStateV1;

typedef struct CanvasBrushDescriptorV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasBrushId brush_id;
    CanvasColorRgba8 color;
    float size;
    float opacity;
    CanvasBrushTip tip;
    float hardness;
    float spacing;
    float taper_start;
    float taper_end;
    float pressure_size_response;
    float pressure_opacity_response;
    float tilt_response;
    CanvasBlendMode blend_mode;
    CanvasResourceId texture_resource_id;
    uint64_t random_seed;
} CanvasBrushDescriptorV1;

typedef struct CanvasEraserDescriptorV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasEraserMode mode;
    float size;
    float hardness;
    CanvasEraserScope scope;
} CanvasEraserDescriptorV1;

typedef struct CanvasCommandV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasCommandType type;
    uint32_t payload_schema_version;
    CanvasByteSpan payload;
} CanvasCommandV1;

typedef struct CanvasSelectionSummaryV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t count;
    CanvasObjectType common_type;
    CanvasSelectionCapabilities capabilities;
    CanvasRectF world_bounds;
} CanvasSelectionSummaryV1;

typedef struct CanvasToolStateV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasToolType active_tool;
} CanvasToolStateV1;

typedef struct CanvasFrameTimingV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t frame_id;
    uint64_t vsync_time_ns;
    uint64_t predicted_present_time_ns;
    uint64_t refresh_period_ns;
    uint32_t target_generation;
} CanvasFrameTimingV1;

typedef struct CanvasResourceVersionV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    CanvasResourceId resource_id;
    uint64_t resource_revision;
    CanvasHash256 content_hash;
} CanvasResourceVersionV1;

struct CanvasEventHeaderV1 {
    uint32_t event_size;
    uint32_t abi_version;
    CanvasEventType type;
    CanvasDocumentHandle document;
    CanvasViewHandle view;
    uint64_t revision;
};

typedef struct CanvasLocalOperationCommittedEventV1 {
    CanvasEventHeaderV1 header;
    CanvasOperationBatchV1 batch;
} CanvasLocalOperationCommittedEventV1;

typedef struct CanvasResourceRequestedEventV1 {
    CanvasEventHeaderV1 header;
    CanvasResourcePurpose purpose;
    CanvasResourceVersionV1 resource;
} CanvasResourceRequestedEventV1;

typedef struct CanvasOperationDurabilityChangedEventV1 {
    CanvasEventHeaderV1 header;
    CanvasOperationId operation_id;
    CanvasOperationDurability durability;
} CanvasOperationDurabilityChangedEventV1;

typedef struct CanvasLocalPresenceChangedEventV1 {
    CanvasEventHeaderV1 header;
    CanvasEncodedDataV1 presence;
} CanvasLocalPresenceChangedEventV1;

typedef struct CanvasDiagnosticEventV1 {
    CanvasEventHeaderV1 header;
    CanvasStatus status;
    CanvasStringView message;
} CanvasDiagnosticEventV1;

CANVAS_RUNTIME_API uint32_t canvas_runtime_get_abi_version(void);
CANVAS_RUNTIME_API const char* canvas_status_message(CanvasStatus status);
CANVAS_RUNTIME_API CanvasStatus canvas_get_last_error(CanvasMutableByteBuffer buffer,
                                                      uint64_t* out_required_size);

CANVAS_RUNTIME_API CanvasStatus canvas_runtime_create(const CanvasRuntimeConfigV1* config,
                                                      CanvasRuntimeHandle* out_runtime);
CANVAS_RUNTIME_API CanvasStatus canvas_runtime_destroy(CanvasRuntimeHandle runtime);
CANVAS_RUNTIME_API CanvasStatus canvas_runtime_get_capabilities(
    CanvasRuntimeHandle runtime, CanvasCapabilitiesV1* in_out_capabilities);
CANVAS_RUNTIME_API CanvasStatus canvas_runtime_copy_diagnostics(
    CanvasRuntimeHandle runtime, CanvasMutableByteBuffer buffer, uint64_t* out_required_size);

CANVAS_RUNTIME_API CanvasStatus canvas_document_open(CanvasRuntimeHandle runtime,
                                                     const CanvasDocumentOpenInfoV1* info,
                                                     CanvasDocumentHandle* out_document);
CANVAS_RUNTIME_API CanvasStatus canvas_document_close(CanvasDocumentHandle document);
CANVAS_RUNTIME_API CanvasStatus canvas_document_get_state(CanvasDocumentHandle document,
                                                          CanvasDocumentStateV1* in_out_state);
CANVAS_RUNTIME_API CanvasStatus canvas_document_copy_recovery_frontier(
    CanvasDocumentHandle document, CanvasMutableByteBuffer buffer, uint64_t* out_required_size);
CANVAS_RUNTIME_API CanvasStatus canvas_document_create_snapshot(
    CanvasDocumentHandle document, const CanvasSnapshotWriteOptionsV1* options,
    CanvasMutableByteBuffer buffer, uint64_t* out_required_size);
CANVAS_RUNTIME_API CanvasStatus canvas_document_apply_remote_operations(
    CanvasDocumentHandle document, const CanvasOperationBatchV1* batch);
CANVAS_RUNTIME_API CanvasStatus canvas_document_update_operation_durability(
    CanvasDocumentHandle document, CanvasOperationId operation_id,
    CanvasOperationDurability durability);
CANVAS_RUNTIME_API CanvasStatus canvas_document_apply_remote_presence(
    CanvasDocumentHandle document, const CanvasEncodedDataV1* presence_batch);

CANVAS_RUNTIME_API CanvasStatus canvas_document_provide_resource(
    CanvasDocumentHandle document, const CanvasResourceVersionV1* resource, CanvasByteSpan bytes);
CANVAS_RUNTIME_API CanvasStatus canvas_document_fail_resource(
    CanvasDocumentHandle document, const CanvasResourceVersionV1* resource, CanvasStatus reason);
CANVAS_RUNTIME_API CanvasStatus canvas_document_evict_resource(
    CanvasDocumentHandle document, const CanvasResourceVersionV1* resource);

CANVAS_RUNTIME_API CanvasStatus canvas_view_create(CanvasDocumentHandle document,
                                                   const CanvasViewConfigV1* config,
                                                   CanvasViewHandle* out_view);
CANVAS_RUNTIME_API CanvasStatus canvas_view_destroy(CanvasViewHandle view);
CANVAS_RUNTIME_API CanvasStatus canvas_view_set_viewport(CanvasViewHandle view,
                                                         const CanvasViewportV1* viewport);
CANVAS_RUNTIME_API CanvasStatus canvas_view_set_camera(CanvasViewHandle view,
                                                       const CanvasCameraV1* camera);
CANVAS_RUNTIME_API CanvasStatus canvas_view_get_camera(CanvasViewHandle view,
                                                       CanvasCameraStateV1* in_out_camera);
CANVAS_RUNTIME_API CanvasStatus canvas_view_pan_by(CanvasViewHandle view, float dx_logical,
                                                   float dy_logical);
CANVAS_RUNTIME_API CanvasStatus canvas_view_zoom_at(CanvasViewHandle view, float x_logical,
                                                    float y_logical, float scale_delta);
CANVAS_RUNTIME_API CanvasStatus canvas_view_screen_to_world(CanvasViewHandle view,
                                                            CanvasPointF view_logical_point,
                                                            CanvasPointF* out_world_point);
CANVAS_RUNTIME_API CanvasStatus canvas_view_world_to_screen(CanvasViewHandle view,
                                                            CanvasPointF world_point,
                                                            CanvasPointF* out_view_logical_point);

CANVAS_RUNTIME_API CanvasStatus canvas_view_attach_surface(
    CanvasViewHandle view, CanvasSurfaceHandle surface, const CanvasSurfaceStateV1* state);
CANVAS_RUNTIME_API CanvasStatus canvas_view_update_surface(
    CanvasViewHandle view, const CanvasSurfaceStateV1* state);
CANVAS_RUNTIME_API CanvasStatus canvas_view_detach_surface(CanvasViewHandle view);

CANVAS_RUNTIME_API CanvasStatus canvas_view_push_pointer_batch(
    CanvasViewHandle view, const CanvasPointerBatchV1* batch);
CANVAS_RUNTIME_API CanvasStatus canvas_view_push_wheel_event(CanvasViewHandle view,
                                                             const CanvasWheelEventV1* event);
CANVAS_RUNTIME_API CanvasStatus canvas_view_push_key_event(CanvasViewHandle view,
                                                           const CanvasKeyEventV1* event);
CANVAS_RUNTIME_API CanvasStatus canvas_view_push_ime_event(CanvasViewHandle view,
                                                           const CanvasImeEventV1* event);
CANVAS_RUNTIME_API CanvasStatus canvas_view_get_text_input_state(
    CanvasViewHandle view, CanvasTextInputStateV1* in_out_state);
CANVAS_RUNTIME_API CanvasStatus canvas_view_copy_surrounding_text(
    CanvasViewHandle view, CanvasMutableByteBuffer buffer, uint64_t* out_required_size,
    uint64_t* out_text_revision);

CANVAS_RUNTIME_API CanvasStatus canvas_view_set_active_tool(CanvasViewHandle view,
                                                            CanvasToolType tool);
CANVAS_RUNTIME_API CanvasStatus canvas_view_set_brush(CanvasViewHandle view,
                                                      const CanvasBrushDescriptorV1* brush);
CANVAS_RUNTIME_API CanvasStatus canvas_view_set_eraser(CanvasViewHandle view,
                                                       const CanvasEraserDescriptorV1* eraser);
CANVAS_RUNTIME_API CanvasStatus canvas_view_get_tool_state(CanvasViewHandle view,
                                                           CanvasToolStateV1* in_out_state);
CANVAS_RUNTIME_API CanvasStatus canvas_view_get_brush(
    CanvasViewHandle view, CanvasBrushDescriptorV1* in_out_brush);
CANVAS_RUNTIME_API CanvasStatus canvas_view_get_eraser(
    CanvasViewHandle view, CanvasEraserDescriptorV1* in_out_eraser);

CANVAS_RUNTIME_API CanvasStatus canvas_view_execute_command(CanvasViewHandle view,
                                                            const CanvasCommandV1* command);
CANVAS_RUNTIME_API CanvasStatus canvas_view_undo(CanvasViewHandle view);
CANVAS_RUNTIME_API CanvasStatus canvas_view_redo(CanvasViewHandle view);
CANVAS_RUNTIME_API CanvasStatus canvas_view_begin_command_batch(CanvasViewHandle view);
CANVAS_RUNTIME_API CanvasStatus canvas_view_end_command_batch(CanvasViewHandle view);
CANVAS_RUNTIME_API CanvasStatus canvas_view_cancel_command_batch(CanvasViewHandle view);

CANVAS_RUNTIME_API CanvasStatus canvas_view_get_selection_summary(
    CanvasViewHandle view, CanvasSelectionSummaryV1* in_out_summary);
CANVAS_RUNTIME_API CanvasStatus canvas_view_copy_selection_ids(
    CanvasViewHandle view, CanvasObjectId* object_ids, uint64_t capacity,
    uint64_t* out_required_count);
CANVAS_RUNTIME_API CanvasStatus canvas_view_set_local_presence(
    CanvasViewHandle view, const CanvasEncodedDataV1* presence);

CANVAS_RUNTIME_API CanvasStatus canvas_view_on_vsync(CanvasViewHandle view,
                                                     const CanvasFrameTimingV1* timing,
                                                     CanvasFrameDecision* out_decision);
CANVAS_RUNTIME_API CanvasStatus canvas_view_render_frame(CanvasViewHandle view,
                                                         const CanvasFrameTimingV1* timing);
CANVAS_RUNTIME_API CanvasStatus canvas_view_set_debug_overlay(CanvasViewHandle view,
                                                              CanvasDebugOverlayFlags flags);

#ifdef __cplusplus
}  // extern "C"
#endif
