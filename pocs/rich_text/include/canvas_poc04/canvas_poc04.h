#ifndef CANVAS_POC04_C_API_H_
#define CANVAS_POC04_C_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANVAS_POC04_ABI_VERSION 1u

typedef uint32_t canvas_poc04_handle_t;

typedef enum canvas_poc04_status {
  CANVAS_POC04_STATUS_OK = 0,
  CANVAS_POC04_STATUS_INVALID_ARGUMENT = 1,
  CANVAS_POC04_STATUS_INVALID_HANDLE = 2,
  CANVAS_POC04_STATUS_INVALID_STATE = 3,
  CANVAS_POC04_STATUS_BUFFER_TOO_SMALL = 4,
  CANVAS_POC04_STATUS_SEQUENCE_ERROR = 5,
  CANVAS_POC04_STATUS_SCHEMA_ERROR = 6,
  CANVAS_POC04_STATUS_INTERNAL_ERROR = 7
} canvas_poc04_status_t;

typedef struct canvas_poc04_create_info {
  uint32_t struct_size;
  uint32_t abi_version;
} canvas_poc04_create_info_t;

typedef struct canvas_poc04_utf16_range {
  uint64_t location;
  uint64_t length;
} canvas_poc04_utf16_range_t;

typedef struct canvas_poc04_rect {
  float x;
  float y;
  float width;
  float height;
} canvas_poc04_rect_t;

/* Experimental POC ABI. No source or binary compatibility is promised. */
canvas_poc04_status_t canvas_poc04_session_create(
    const canvas_poc04_create_info_t* info, canvas_poc04_handle_t* out_session);
canvas_poc04_status_t canvas_poc04_session_destroy(canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_focus(canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_blur(canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_set_selection(
    canvas_poc04_handle_t session, uint32_t anchor_paragraph,
    uint32_t anchor_offset_utf16, uint32_t focus_paragraph,
    uint32_t focus_offset_utf16);
canvas_poc04_status_t canvas_poc04_session_set_selection_flat_utf16(
    canvas_poc04_handle_t session, uint64_t anchor_utf16,
    uint64_t focus_utf16);
canvas_poc04_status_t canvas_poc04_session_selection_flat_utf16(
    canvas_poc04_handle_t session, canvas_poc04_utf16_range_t* out_range);
canvas_poc04_status_t canvas_poc04_session_begin_composition(
    canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_update_composition_utf8(
    canvas_poc04_handle_t session, const char* text, size_t text_size);
canvas_poc04_status_t canvas_poc04_session_update_composition_utf8_with_selection(
    canvas_poc04_handle_t session, const char* text, size_t text_size,
    uint32_t selection_start_utf16, uint32_t selection_end_utf16);
canvas_poc04_status_t canvas_poc04_session_commit_composition(
    canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_cancel_composition(
    canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_insert_utf8(
    canvas_poc04_handle_t session, const char* text, size_t text_size);
canvas_poc04_status_t canvas_poc04_session_delete_selection(
    canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_delete_surrounding_utf16(
    canvas_poc04_handle_t session, uint32_t before_utf16,
    uint32_t after_utf16);
canvas_poc04_status_t canvas_poc04_session_undo(canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_redo(canvas_poc04_handle_t session);
canvas_poc04_status_t canvas_poc04_session_selected_text_utf8(
    canvas_poc04_handle_t session, char* buffer, size_t buffer_size,
    size_t* out_required_size);
canvas_poc04_status_t canvas_poc04_session_text_before_cursor_utf8(
    canvas_poc04_handle_t session, uint32_t max_utf16_units, char* buffer,
    size_t buffer_size, size_t* out_required_size);
canvas_poc04_status_t canvas_poc04_session_text_after_cursor_utf8(
    canvas_poc04_handle_t session, uint32_t max_utf16_units, char* buffer,
    size_t buffer_size, size_t* out_required_size);
canvas_poc04_status_t canvas_poc04_session_surrounding_text_utf8(
    canvas_poc04_handle_t session, uint32_t max_utf16_units, char* buffer,
    size_t buffer_size, size_t* out_required_size);
/* Queries below expose the composition-aware text presentation required by
 * NSTextInputClient and UITextInput. Offsets are flat UTF-16 code units. */
canvas_poc04_status_t canvas_poc04_session_presented_utf16_length(
    canvas_poc04_handle_t session, uint64_t* out_length);
canvas_poc04_status_t canvas_poc04_session_presented_text_range_utf8(
    canvas_poc04_handle_t session, uint64_t location_utf16,
    uint64_t length_utf16, char* buffer, size_t buffer_size,
    size_t* out_required_size);
canvas_poc04_status_t canvas_poc04_session_replace_range_utf8(
    canvas_poc04_handle_t session, uint64_t location_utf16,
    uint64_t length_utf16, const char* text, size_t text_size);
canvas_poc04_status_t canvas_poc04_session_composition_range_flat_utf16(
    canvas_poc04_handle_t session, canvas_poc04_utf16_range_t* out_range,
    uint32_t* out_active);
canvas_poc04_status_t canvas_poc04_session_composition_selection_utf16(
    canvas_poc04_handle_t session, canvas_poc04_utf16_range_t* out_range,
    uint32_t* out_active);
canvas_poc04_status_t canvas_poc04_session_first_rect_for_range_utf16(
    canvas_poc04_handle_t session, uint64_t location_utf16,
    uint64_t length_utf16, float layout_width, canvas_poc04_rect_t* out_rect,
    canvas_poc04_utf16_range_t* out_actual_range);
canvas_poc04_status_t canvas_poc04_session_caret_rect_for_offset_utf16(
    canvas_poc04_handle_t session, uint64_t offset_utf16, float layout_width,
    canvas_poc04_rect_t* out_rect);
canvas_poc04_status_t canvas_poc04_session_character_offset_for_point(
    canvas_poc04_handle_t session, float x, float y, float layout_width,
    uint64_t* out_offset_utf16);
canvas_poc04_status_t canvas_poc04_document_digest(
    canvas_poc04_handle_t session, char* buffer, size_t buffer_size,
    size_t* out_required_size);
canvas_poc04_status_t canvas_poc04_operation_log_ndjson(
    canvas_poc04_handle_t session, char* buffer, size_t buffer_size,
    size_t* out_required_size);
const char* canvas_poc04_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  // CANVAS_POC04_C_API_H_
