#ifndef CANVAS_POC_CANVAS_POC_H_
#define CANVAS_POC_CANVAS_POC_H_

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(CANVAS_POC01_SHARED_LIBRARY)
#if defined(CANVAS_POC01_IMPLEMENTATION)
#define CANVAS_POC_API __declspec(dllexport)
#else
#define CANVAS_POC_API __declspec(dllimport)
#endif
#else
#define CANVAS_POC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CANVAS_POC_ABI_VERSION 1u

typedef uint32_t canvas_poc_handle_t;

typedef enum canvas_poc_status_t {
  CANVAS_POC_STATUS_OK = 0,
  CANVAS_POC_STATUS_INVALID_ARGUMENT = 1,
  CANVAS_POC_STATUS_ABI_MISMATCH = 2,
  CANVAS_POC_STATUS_INVALID_HANDLE = 3,
  CANVAS_POC_STATUS_NOT_FOUND = 4,
  CANVAS_POC_STATUS_ALREADY_EXISTS = 5,
  CANVAS_POC_STATUS_BUFFER_TOO_SMALL = 6,
  CANVAS_POC_STATUS_PARSE_ERROR = 7,
  CANVAS_POC_STATUS_SEQUENCE_ERROR = 8,
  CANVAS_POC_STATUS_ASSET_ERROR = 9,
  CANVAS_POC_STATUS_RENDER_ERROR = 10,
  CANVAS_POC_STATUS_PLATFORM_ERROR = 11,
  CANVAS_POC_STATUS_INTERNAL_ERROR = 12
} canvas_poc_status_t;

typedef void (*canvas_poc_log_fn)(void* user_data, uint32_t level,
                                  const char* message, size_t message_size);

typedef struct canvas_poc_runtime_config_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void* user_data;
  canvas_poc_log_fn log;
} canvas_poc_runtime_config_v1;

typedef struct canvas_poc_document_config_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t page_width;
  uint32_t page_height;
  uint8_t background_rgba[4];
} canvas_poc_document_config_v1;

typedef struct canvas_poc_view_config_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t width;
  uint32_t height;
  float device_pixel_ratio;
} canvas_poc_view_config_v1;

CANVAS_POC_API canvas_poc_status_t canvas_poc_runtime_create(
    const canvas_poc_runtime_config_v1* config,
    canvas_poc_handle_t* out_runtime);
CANVAS_POC_API canvas_poc_status_t canvas_poc_runtime_destroy(
    canvas_poc_handle_t runtime);
CANVAS_POC_API canvas_poc_status_t canvas_poc_runtime_register_asset(
    canvas_poc_handle_t runtime, const char* key, size_t key_size,
    const uint8_t* bytes, size_t byte_count);

CANVAS_POC_API canvas_poc_status_t canvas_poc_document_create(
    canvas_poc_handle_t runtime, const canvas_poc_document_config_v1* config,
    canvas_poc_handle_t* out_document);
CANVAS_POC_API canvas_poc_status_t canvas_poc_document_destroy(
    canvas_poc_handle_t document);
CANVAS_POC_API canvas_poc_status_t canvas_poc_document_apply_ndjson(
    canvas_poc_handle_t document, const char* ndjson, size_t ndjson_size);
CANVAS_POC_API canvas_poc_status_t canvas_poc_document_digest(
    canvas_poc_handle_t document, char* buffer, size_t buffer_size,
    size_t* out_required_size);
CANVAS_POC_API canvas_poc_status_t canvas_poc_document_sequence(
    canvas_poc_handle_t document, uint64_t* out_sequence);

/* Offscreen view used by deterministic host tests. Web, Windows, Apple, and
 * Android surfaces are created by separate adapters and never enter this API. */
CANVAS_POC_API canvas_poc_status_t canvas_poc_view_create_offscreen(
    canvas_poc_handle_t document, const canvas_poc_view_config_v1* config,
    canvas_poc_handle_t* out_view);
CANVAS_POC_API canvas_poc_status_t canvas_poc_view_destroy(
    canvas_poc_handle_t view);
CANVAS_POC_API canvas_poc_status_t canvas_poc_view_resize(
    canvas_poc_handle_t view, uint32_t width, uint32_t height,
    float device_pixel_ratio);
CANVAS_POC_API canvas_poc_status_t canvas_poc_view_render(
    canvas_poc_handle_t view);
CANVAS_POC_API canvas_poc_status_t canvas_poc_view_read_rgba(
    canvas_poc_handle_t view, uint8_t* buffer, size_t buffer_size,
    size_t* out_required_size);

CANVAS_POC_API const char* canvas_poc_status_message(
    canvas_poc_status_t status);
CANVAS_POC_API canvas_poc_status_t canvas_poc_last_error(
    char* buffer, size_t buffer_size, size_t* out_required_size);

#ifdef __cplusplus
}
#endif

#endif  // CANVAS_POC_CANVAS_POC_H_
