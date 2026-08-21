#ifndef ARC_PROTOCOL_H_
#define ARC_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARC_ABI_VERSION 0u
#define ARC_PROTOCOL_SCHEMA_VERSION 0u

typedef enum arc_status_t {
  ARC_STATUS_OK = 0,
  ARC_STATUS_INVALID_ARGUMENT = 1,
  ARC_STATUS_ABI_MISMATCH = 2,
  ARC_STATUS_INVALID_STATE = 3,
  ARC_STATUS_STALE_REVISION = 4,
  ARC_STATUS_NOT_FOUND = 5,
  ARC_STATUS_CAPACITY_EXCEEDED = 6,
  ARC_STATUS_BACKEND_UNAVAILABLE = 7,
  ARC_STATUS_PRESENTATION_FAILED = 8,
  ARC_STATUS_SURFACE_LOST = 9,
  ARC_STATUS_INTERNAL_ERROR = 10
} arc_status_t;

typedef enum arc_coordinate_space_t {
  ARC_COORDINATE_SPACE_WORLD = 1,
  ARC_COORDINATE_SPACE_VIEW_LOGICAL = 2,
  ARC_COORDINATE_SPACE_DEVICE_PIXEL = 3
} arc_coordinate_space_t;

typedef enum arc_preview_primitive_kind_t {
  ARC_PREVIEW_PRIMITIVE_VECTOR_POINT = 1,
  ARC_PREVIEW_PRIMITIVE_DAB = 2
} arc_preview_primitive_kind_t;

typedef enum arc_input_tool_t {
  ARC_INPUT_TOOL_MOUSE = 1,
  ARC_INPUT_TOOL_PEN = 2,
  ARC_INPUT_TOOL_TOUCH = 3
} arc_input_tool_t;

typedef enum arc_pointer_phase_t {
  ARC_POINTER_PHASE_DOWN = 1,
  ARC_POINTER_PHASE_MOVE = 2,
  ARC_POINTER_PHASE_UP = 3,
  ARC_POINTER_PHASE_CANCEL = 4,
  ARC_POINTER_PHASE_HOVER = 5
} arc_pointer_phase_t;

typedef enum arc_sample_provenance_t {
  ARC_SAMPLE_CONFIRMED_CURRENT = 1,
  ARC_SAMPLE_CONFIRMED_COALESCED = 2,
  ARC_SAMPLE_PLATFORM_PREDICTION_HINT = 3
} arc_sample_provenance_t;

typedef enum arc_presentation_evidence_t {
  ARC_EVIDENCE_NONE = 0,
  ARC_EVIDENCE_RENDER_COMPLETE = 1,
  ARC_EVIDENCE_GPU_SUBMITTED = 2,
  ARC_EVIDENCE_PRESENT_ACCEPTED = 3,
  ARC_EVIDENCE_COMPOSITOR_VISIBLE = 4,
  ARC_EVIDENCE_PHOTOMETRIC = 5,
  ARC_EVIDENCE_DETERMINISTIC_ORACLE = 6
} arc_presentation_evidence_t;

typedef enum arc_platform_kind_t {
  ARC_PLATFORM_HEADLESS = 1,
  ARC_PLATFORM_WEB = 2,
  ARC_PLATFORM_WINDOWS = 3,
  ARC_PLATFORM_ANDROID = 4,
  ARC_PLATFORM_MACOS = 5,
  ARC_PLATFORM_IOS = 6,
  ARC_PLATFORM_CHROMIUMOS = 7,
  ARC_PLATFORM_DEVICE_DIRECT_PLANE = 8,
  ARC_PLATFORM_IPADOS = 9
} arc_platform_kind_t;

enum arc_input_capability_bits {
  ARC_INPUT_CAPABILITY_PRESSURE = 1ull << 0u,
  ARC_INPUT_CAPABILITY_TILT = 1ull << 1u,
  ARC_INPUT_CAPABILITY_CONTACT = 1ull << 2u,
  ARC_INPUT_CAPABILITY_HISTORY = 1ull << 3u,
  ARC_INPUT_CAPABILITY_COALESCED = 1ull << 4u,
  ARC_INPUT_CAPABILITY_PREDICTION_HINT = 1ull << 5u,
  ARC_INPUT_CAPABILITY_HOVER = 1ull << 6u,
  ARC_INPUT_CAPABILITY_ERASER = 1ull << 7u
};

enum arc_presentation_capability_bits {
  ARC_PRESENTATION_CAPABILITY_INDEPENDENT_TARGET = 1ull << 0u,
  ARC_PRESENTATION_CAPABILITY_REPLACE_TRUNCATE = 1ull << 1u,
  ARC_PRESENTATION_CAPABILITY_PRESENT_RECEIPT = 1ull << 2u,
  ARC_PRESENTATION_CAPABILITY_FRONT_BUFFER = 1ull << 3u,
  ARC_PRESENTATION_CAPABILITY_DESYNCHRONIZED = 1ull << 4u,
  ARC_PRESENTATION_CAPABILITY_SHARED_GPU_DEVICE = 1ull << 5u,
  ARC_PRESENTATION_CAPABILITY_TEARING_RISK = 1ull << 6u
};

typedef struct arc_handoff_token_v0 {
  uint64_t high;
  uint64_t low;
} arc_handoff_token_v0;

typedef struct arc_affine_transform_v0 {
  float m00;
  float m01;
  float m10;
  float m11;
  float tx;
  float ty;
} arc_affine_transform_v0;

typedef struct arc_pointer_sample_v0 {
  uint64_t pointer_id;
  uint64_t sample_sequence;
  uint64_t timestamp_us;
  float x;
  float y;
  float pressure;
  float tilt_x;
  float tilt_y;
  float contact_width;
  float contact_height;
  uint32_t phase;
  uint32_t provenance;
} arc_pointer_sample_v0;

typedef struct arc_pointer_sample_batch_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t schema_version;
  uint32_t coordinate_space;
  uint64_t view_id;
  uint64_t viewport_revision;
  uint64_t device_id;
  uint64_t input_capabilities;
  uint32_t tool;
  uint32_t reserved;
  arc_affine_transform_v0 view_to_world;
  const arc_pointer_sample_v0* samples;
  uint32_t sample_count;
  uint32_t sample_stride;
} arc_pointer_sample_batch_v0;

typedef struct arc_brush_descriptor_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t schema_version;
  uint32_t brush_type;
  uint32_t brush_version;
  uint32_t algorithm_version;
  uint8_t color_rgba[4];
  float size;
  float spacing;
  float opacity;
  float jitter;
  const char* resource_id;
  uint32_t resource_id_size;
  const char* resource_content_hash;
  uint32_t resource_content_hash_size;
} arc_brush_descriptor_v0;

typedef struct arc_preview_primitive_v0 {
  uint32_t kind;
  uint32_t reserved;
  float x;
  float y;
  float radius;
  float rotation_degrees;
  float opacity;
} arc_preview_primitive_v0;

typedef struct arc_preview_target_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t platform_kind;
  uint32_t color_space;
  uint64_t target_id;
  uint64_t target_generation;
  uint32_t width_pixels;
  uint32_t height_pixels;
  float device_pixel_ratio;
  uint32_t reserved;
  uint64_t opaque_platform_handle;
} arc_preview_target_v0;

typedef struct arc_preview_begin_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t schema_version;
  uint32_t coordinate_space;
  uint64_t stroke_id;
  uint64_t view_id;
  uint64_t viewport_revision;
  uint64_t target_generation;
  arc_affine_transform_v0 source_to_device;
  arc_brush_descriptor_v0 brush;
} arc_preview_begin_v0;

typedef struct arc_preview_update_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t schema_version;
  uint32_t coordinate_space;
  uint64_t stroke_id;
  uint64_t preview_revision;
  uint64_t view_id;
  uint64_t viewport_revision;
  uint64_t target_generation;
  arc_affine_transform_v0 source_to_device;
  uint32_t truncate_confirmed_to;
  uint32_t reserved;
  const arc_preview_primitive_v0* confirmed_append;
  uint32_t confirmed_append_count;
  uint32_t confirmed_append_stride;
  const arc_preview_primitive_v0* predicted_tail;
  uint32_t predicted_tail_count;
  uint32_t predicted_tail_stride;
} arc_preview_update_v0;

typedef struct arc_preview_seal_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t stroke_id;
  uint64_t final_preview_revision;
  uint64_t target_generation;
} arc_preview_seal_v0;

typedef struct arc_canonical_commit_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t stroke_id;
  uint64_t final_preview_revision;
  uint64_t document_revision;
  uint64_t target_generation;
  arc_handoff_token_v0 handoff_token;
} arc_canonical_commit_v0;

typedef struct arc_presentation_receipt_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t evidence;
  uint32_t status;
  uint64_t target_generation;
  uint64_t presentation_id;
  uint64_t submit_timestamp_us;
  uint64_t present_timestamp_us;
} arc_presentation_receipt_v0;

typedef struct arc_canonical_visible_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t stroke_id;
  uint64_t document_revision;
  uint64_t target_generation;
  arc_handoff_token_v0 handoff_token;
  arc_presentation_receipt_v0 receipt;
} arc_canonical_visible_v0;

typedef struct arc_preview_cancel_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t stroke_id;
  uint64_t target_generation;
  uint32_t reason;
  uint32_t reserved;
} arc_preview_cancel_v0;

typedef struct arc_backend_capabilities_v0 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t platform_kind;
  uint32_t reserved;
  uint64_t input_capabilities;
  uint64_t presentation_capabilities;
  uint32_t max_pending_strokes;
  uint32_t max_primitives_per_update;
  uint64_t max_queue_bytes;
} arc_backend_capabilities_v0;

#ifdef __cplusplus
}
#endif

#endif  // ARC_PROTOCOL_H_
