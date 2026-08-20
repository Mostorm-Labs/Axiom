#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <emscripten/emscripten.h>

#include "canvas_poc04/canvas_poc04.h"
#if defined(CANVAS_POC04_FONT_PATH)
#include "canonical_behavior_fixture.h"
#endif

extern "C" {

EMSCRIPTEN_KEEPALIVE canvas_poc04_handle_t canvas_poc04_web_create() {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  return canvas_poc04_session_create(&info, &session) == CANVAS_POC04_STATUS_OK
             ? session
             : 0;
}

EMSCRIPTEN_KEEPALIVE void canvas_poc04_web_destroy(
    canvas_poc04_handle_t session) {
  static_cast<void>(canvas_poc04_session_destroy(session));
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_focus(canvas_poc04_handle_t session) {
  return canvas_poc04_session_focus(session);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_blur(canvas_poc04_handle_t session) {
  return canvas_poc04_session_blur(session);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_begin_composition(
    canvas_poc04_handle_t session) {
  return canvas_poc04_session_begin_composition(session);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_update_composition(
    canvas_poc04_handle_t session, const char* utf8, size_t size) {
  return canvas_poc04_session_update_composition_utf8(session, utf8, size);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_update_composition_with_selection(
    canvas_poc04_handle_t session, const char* utf8, size_t size,
    uint32_t selection_start_utf16, uint32_t selection_end_utf16) {
  return canvas_poc04_session_update_composition_utf8_with_selection(
      session, utf8, size, selection_start_utf16, selection_end_utf16);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_commit_composition(
    canvas_poc04_handle_t session) {
  return canvas_poc04_session_commit_composition(session);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_cancel_composition(
    canvas_poc04_handle_t session) {
  return canvas_poc04_session_cancel_composition(session);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_insert(
    canvas_poc04_handle_t session, const char* utf8, size_t size) {
  return canvas_poc04_session_insert_utf8(session, utf8, size);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_delete_surrounding(
    canvas_poc04_handle_t session, uint32_t before_utf16,
    uint32_t after_utf16) {
  return canvas_poc04_session_delete_surrounding_utf16(
      session, before_utf16, after_utf16);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_undo(
    canvas_poc04_handle_t session) {
  return canvas_poc04_session_undo(session);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc04_web_redo(
    canvas_poc04_handle_t session) {
  return canvas_poc04_session_redo(session);
}

EMSCRIPTEN_KEEPALIVE const char* canvas_poc04_web_digest(
    canvas_poc04_handle_t session) {
  static thread_local std::array<char, 33> digest{};
  size_t required = 0;
  if (canvas_poc04_document_digest(session, digest.data(), digest.size(),
                                   &required) != CANVAS_POC04_STATUS_OK) {
    return "";
  }
  return digest.data();
}

EMSCRIPTEN_KEEPALIVE const char* canvas_poc04_web_presented_text(
    canvas_poc04_handle_t session) {
  static thread_local std::string text;
  uint64_t length = 0;
  size_t required = 0;
  if (canvas_poc04_session_presented_utf16_length(session, &length) !=
          CANVAS_POC04_STATUS_OK ||
      canvas_poc04_session_presented_text_range_utf8(
          session, 0, length, nullptr, 0, &required) !=
          CANVAS_POC04_STATUS_BUFFER_TOO_SMALL ||
      required == 0) {
    text.clear();
    return text.c_str();
  }
  text.assign(required, '\0');
  if (canvas_poc04_session_presented_text_range_utf8(
          session, 0, length, text.data(), text.size(), &required) !=
      CANVAS_POC04_STATUS_OK) {
    text.clear();
    return text.c_str();
  }
  text.resize(required - 1);
  return text.c_str();
}

canvas_poc04_utf16_range_t WebSelection(canvas_poc04_handle_t session) {
  canvas_poc04_utf16_range_t selection{};
  static_cast<void>(
      canvas_poc04_session_selection_flat_utf16(session, &selection));
  return selection;
}

canvas_poc04_rect_t WebCaret(canvas_poc04_handle_t session,
                             float layout_width) {
  const canvas_poc04_utf16_range_t selection = WebSelection(session);
  canvas_poc04_rect_t caret{};
  static_cast<void>(canvas_poc04_session_caret_rect_for_offset_utf16(
      session, selection.location + selection.length, layout_width, &caret));
  return caret;
}

EMSCRIPTEN_KEEPALIVE double canvas_poc04_web_selection_location(
    canvas_poc04_handle_t session) {
  return static_cast<double>(WebSelection(session).location);
}

EMSCRIPTEN_KEEPALIVE double canvas_poc04_web_selection_length(
    canvas_poc04_handle_t session) {
  return static_cast<double>(WebSelection(session).length);
}

EMSCRIPTEN_KEEPALIVE float canvas_poc04_web_caret_x(
    canvas_poc04_handle_t session, float layout_width) {
  return WebCaret(session, layout_width).x;
}

EMSCRIPTEN_KEEPALIVE float canvas_poc04_web_caret_y(
    canvas_poc04_handle_t session, float layout_width) {
  return WebCaret(session, layout_width).y;
}

EMSCRIPTEN_KEEPALIVE float canvas_poc04_web_caret_width(
    canvas_poc04_handle_t session, float layout_width) {
  return WebCaret(session, layout_width).width;
}

EMSCRIPTEN_KEEPALIVE float canvas_poc04_web_caret_height(
    canvas_poc04_handle_t session, float layout_width) {
  return WebCaret(session, layout_width).height;
}

#if defined(CANVAS_POC04_FONT_PATH)
EMSCRIPTEN_KEEPALIVE const char* canvas_poc04_web_canonical_behavior_report() {
  static thread_local std::string report;
  try {
    const canvas::poc04::CanonicalBehaviorArtifact artifact =
        canvas::poc04::BuildCanonicalBehaviorArtifact(
            "web", "/fonts/Roboto-Regular.ttf",
            "/fonts/NotoSansCJK-VF-subset.otf.ttc");
    report = artifact.passed ? artifact.json : "";
  } catch (...) {
    report.clear();
  }
  return report.c_str();
}
#endif

}  // extern "C"
