#include <jni.h>

#include <string>

#include "canvas_poc04/canvas_poc04.h"
#include "canvas_poc04/rich_text.h"
#if defined(CANVAS_POC04_HAS_CANONICAL_BEHAVIOR)
#include "canonical_behavior_fixture.h"
#endif

namespace {

bool ToUtf8(JNIEnv* environment, jstring value, std::string* result) {
  if (result == nullptr) return false;
  result->clear();
  if (value == nullptr) return true;
  const jchar* units = environment->GetStringChars(value, nullptr);
  if (units == nullptr) return false;
  const jsize length = environment->GetStringLength(value);
  for (jsize index = 0; index < length; ++index) {
    uint32_t codepoint = units[index];
    if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
      if (index + 1 >= length || units[index + 1] < 0xdc00U ||
          units[index + 1] > 0xdfffU) {
        environment->ReleaseStringChars(value, units);
        return false;
      }
      codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) +
                  (units[++index] - 0xdc00U);
    } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
      environment->ReleaseStringChars(value, units);
      return false;
    }
    if (codepoint <= 0x7fU) {
      result->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
      result->push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
      result->push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
      result->push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
      result->push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
      result->push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
      result->push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
      result->push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
      result->push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
      result->push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
  }
  environment->ReleaseStringChars(value, units);
  return true;
}

jstring QueryText(
    JNIEnv* environment, canvas_poc04_handle_t session,
    canvas_poc04_status_t (*query)(canvas_poc04_handle_t, uint32_t, char*,
                                  size_t, size_t*),
    uint32_t limit) {
  size_t required = 0;
  canvas_poc04_status_t status = query(session, limit, nullptr, 0, &required);
  if (status != CANVAS_POC04_STATUS_BUFFER_TOO_SMALL || required == 0) {
    return nullptr;
  }
  std::string text(required, '\0');
  status = query(session, limit, text.data(), text.size(), &required);
  if (status != CANVAS_POC04_STATUS_OK) return nullptr;
  text.resize(required - 1);
  const std::u16string utf16 = canvas::poc04::Utf8ToUtf16(text);
  return environment->NewString(
      reinterpret_cast<const jchar*>(utf16.data()), utf16.size());
}

jstring QuerySelectedText(JNIEnv* environment, canvas_poc04_handle_t session) {
  size_t required = 0;
  canvas_poc04_status_t status = canvas_poc04_session_selected_text_utf8(
      session, nullptr, 0, &required);
  if (status != CANVAS_POC04_STATUS_BUFFER_TOO_SMALL || required == 0) {
    return nullptr;
  }
  std::string text(required, '\0');
  status = canvas_poc04_session_selected_text_utf8(
      session, text.data(), text.size(), &required);
  if (status != CANVAS_POC04_STATUS_OK) return nullptr;
  text.resize(required - 1);
  const std::u16string utf16 = canvas::poc04::Utf8ToUtf16(text);
  return environment->NewString(
      reinterpret_cast<const jchar*>(utf16.data()), utf16.size());
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeCreate(
    JNIEnv*, jclass) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  return canvas_poc04_session_create(&info, &session) == CANVAS_POC04_STATUS_OK
             ? static_cast<jint>(session)
             : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeDestroy(
    JNIEnv*, jclass, jint session) {
  static_cast<void>(canvas_poc04_session_destroy(session));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeFocus(
    JNIEnv*, jclass, jint session, jboolean focused) {
  return focused ? canvas_poc04_session_focus(session)
                 : canvas_poc04_session_blur(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeSetSelection(
    JNIEnv*, jclass, jint session, jint anchor_paragraph, jint anchor_offset,
    jint focus_paragraph, jint focus_offset) {
  return canvas_poc04_session_set_selection(
      session, anchor_paragraph, anchor_offset, focus_paragraph, focus_offset);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeBeginComposition(
    JNIEnv*, jclass, jint session) {
  return canvas_poc04_session_begin_composition(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeUpdateComposition(
    JNIEnv* environment, jclass, jint session, jstring value,
    jint selection_start, jint selection_end) {
  if (selection_start < 0 || selection_end < selection_start) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  std::string text;
  if (!ToUtf8(environment, value, &text)) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return canvas_poc04_session_update_composition_utf8_with_selection(
      session, text.data(), text.size(), selection_start, selection_end);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeFinishComposition(
    JNIEnv*, jclass, jint session, jboolean commit) {
  return commit ? canvas_poc04_session_commit_composition(session)
                : canvas_poc04_session_cancel_composition(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeInsert(
    JNIEnv* environment, jclass, jint session, jstring value) {
  std::string text;
  if (!ToUtf8(environment, value, &text)) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return canvas_poc04_session_insert_utf8(session, text.data(), text.size());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeDeleteSurrounding(
    JNIEnv*, jclass, jint session, jint before, jint after) {
  if (before < 0 || after < 0) return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  return canvas_poc04_session_delete_surrounding_utf16(
      session, before, after);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeSelectedText(
    JNIEnv* environment, jclass, jint session) {
  return QuerySelectedText(environment, session);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeTextBeforeCursor(
    JNIEnv* environment, jclass, jint session, jint limit) {
  if (limit < 0) return nullptr;
  return QueryText(environment, session,
                   canvas_poc04_session_text_before_cursor_utf8, limit);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeTextAfterCursor(
    JNIEnv* environment, jclass, jint session, jint limit) {
  if (limit < 0) return nullptr;
  return QueryText(environment, session,
                   canvas_poc04_session_text_after_cursor_utf8, limit);
}

#if defined(CANVAS_POC04_HAS_CANONICAL_BEHAVIOR)
extern "C" JNIEXPORT jstring JNICALL
Java_com_mostorm_canvas_poc04_NativeRichText_nativeCanonicalBehaviorReport(
    JNIEnv* environment, jclass, jstring latin_font_path,
    jstring cjk_font_path) {
  std::string latin;
  std::string cjk;
  if (!ToUtf8(environment, latin_font_path, &latin) ||
      !ToUtf8(environment, cjk_font_path, &cjk)) {
    return nullptr;
  }
  try {
    const auto artifact = canvas::poc04::BuildCanonicalBehaviorArtifact(
        "android", std::move(latin), std::move(cjk));
    if (!artifact.passed) return nullptr;
    const std::u16string utf16 = canvas::poc04::Utf8ToUtf16(artifact.json);
    return environment->NewString(
        reinterpret_cast<const jchar*>(utf16.data()), utf16.size());
  } catch (...) {
    return nullptr;
  }
}
#endif
