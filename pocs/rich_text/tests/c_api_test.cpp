#include <array>
#include <string>

#include <gtest/gtest.h>

#include "canvas_poc04/canvas_poc04.h"

namespace {

TEST(CApiTest, ExperimentalAbiSupportsCompositionDigestAndStaleHandles) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  ASSERT_EQ(canvas_poc04_session_create(&info, &session),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_begin_composition(session),
            CANVAS_POC04_STATUS_OK);
  const std::string text = "中文";
  ASSERT_EQ(canvas_poc04_session_update_composition_utf8_with_selection(
                session, text.data(), text.size(), 1, 2),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_commit_composition(session),
            CANVAS_POC04_STATUS_OK);
  size_t required = 0;
  EXPECT_EQ(canvas_poc04_document_digest(session, nullptr, 0, &required),
            CANVAS_POC04_STATUS_BUFFER_TOO_SMALL);
  EXPECT_EQ(required, 33U);
  std::array<char, 33> digest{};
  EXPECT_EQ(canvas_poc04_document_digest(session, digest.data(), digest.size(),
                                         &required),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(canvas_poc04_session_focus(session),
            CANVAS_POC04_STATUS_INVALID_ARGUMENT);
}

TEST(CApiTest, LifecycleOneHundredTimesLeavesNoActiveComposition) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  for (int iteration = 0; iteration < 100; ++iteration) {
    canvas_poc04_handle_t session = 0;
    ASSERT_EQ(canvas_poc04_session_create(&info, &session),
              CANVAS_POC04_STATUS_OK);
    ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);
    ASSERT_EQ(canvas_poc04_session_begin_composition(session),
              CANVAS_POC04_STATUS_OK);
    ASSERT_EQ(canvas_poc04_session_blur(session), CANVAS_POC04_STATUS_OK);
    ASSERT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
  }
}

TEST(CApiTest, SurroundingDeletionAndCursorQueriesUseUtf16Units) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  ASSERT_EQ(canvas_poc04_session_create(&info, &session), CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);
  const std::string text = "a😀中b";
  ASSERT_EQ(canvas_poc04_session_insert_utf8(session, text.data(), text.size()),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_set_selection(session, 0, 4, 0, 4),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_delete_surrounding_utf16(session, 1, 1),
            CANVAS_POC04_STATUS_OK);
  size_t required = 0;
  ASSERT_EQ(canvas_poc04_session_text_before_cursor_utf8(
                session, 16, nullptr, 0, &required),
            CANVAS_POC04_STATUS_BUFFER_TOO_SMALL);
  std::string before(required, '\0');
  ASSERT_EQ(canvas_poc04_session_text_before_cursor_utf8(
                session, 16, before.data(), before.size(), &required),
            CANVAS_POC04_STATUS_OK);
  before.resize(required - 1);
  EXPECT_EQ(before, "a😀");
  ASSERT_EQ(canvas_poc04_session_text_after_cursor_utf8(
                session, 16, nullptr, 0, &required),
            CANVAS_POC04_STATUS_BUFFER_TOO_SMALL);
  std::string after(required, '\0');
  ASSERT_EQ(canvas_poc04_session_text_after_cursor_utf8(
                session, 16, after.data(), after.size(), &required),
            CANVAS_POC04_STATUS_OK);
  after.resize(required - 1);
  EXPECT_EQ(after, "");
  EXPECT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
}

TEST(CApiTest, AppleQueriesExposeCompositionAwareFlatUtf16StateAndGeometry) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  ASSERT_EQ(canvas_poc04_session_create(&info, &session), CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);
  const std::string initial = "ab😀cd";
  ASSERT_EQ(canvas_poc04_session_insert_utf8(session, initial.data(), initial.size()),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_set_selection_flat_utf16(session, 2, 4),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_begin_composition(session), CANVAS_POC04_STATUS_OK);
  const std::string marked = "中文";
  ASSERT_EQ(canvas_poc04_session_update_composition_utf8_with_selection(
                session, marked.data(), marked.size(), 1, 2),
            CANVAS_POC04_STATUS_OK);

  uint64_t length = 0;
  ASSERT_EQ(canvas_poc04_session_presented_utf16_length(session, &length),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(length, 6U);
  canvas_poc04_utf16_range_t range{};
  uint32_t active = 0;
  ASSERT_EQ(canvas_poc04_session_composition_range_flat_utf16(
                session, &range, &active), CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(active, 1U);
  EXPECT_EQ(range.location, 2U);
  EXPECT_EQ(range.length, 2U);
  ASSERT_EQ(canvas_poc04_session_selection_flat_utf16(session, &range),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(range.location, 3U);
  EXPECT_EQ(range.length, 1U);

  size_t required = 0;
  ASSERT_EQ(canvas_poc04_session_presented_text_range_utf8(
                session, 0, length, nullptr, 0, &required),
            CANVAS_POC04_STATUS_BUFFER_TOO_SMALL);
  std::string presented(required, '\0');
  ASSERT_EQ(canvas_poc04_session_presented_text_range_utf8(
                session, 0, length, presented.data(), presented.size(), &required),
            CANVAS_POC04_STATUS_OK);
  presented.resize(required - 1);
  EXPECT_EQ(presented, "ab中文cd");

  canvas_poc04_rect_t rect{};
  ASSERT_EQ(canvas_poc04_session_caret_rect_for_offset_utf16(
                session, 4, 400.0F, &rect), CANVAS_POC04_STATUS_OK);
  EXPECT_GT(rect.x, 0.0F);
  uint64_t hit = 0;
  ASSERT_EQ(canvas_poc04_session_character_offset_for_point(
                session, rect.x, rect.y, 400.0F, &hit),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(hit, 4U);
  EXPECT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
}

TEST(CApiTest, FlatReplacementRejectsSurrogateSplits) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  ASSERT_EQ(canvas_poc04_session_create(&info, &session), CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);
  const std::string initial = "a😀b";
  ASSERT_EQ(canvas_poc04_session_insert_utf8(session, initial.data(), initial.size()),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(canvas_poc04_session_replace_range_utf8(session, 2, 0, "x", 1),
            CANVAS_POC04_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(canvas_poc04_session_replace_range_utf8(session, 1, 2, "中", 3),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
}

TEST(CApiTest, NewlineMovesSelectionAndCaretToNextVisualLine) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  ASSERT_EQ(canvas_poc04_session_create(&info, &session),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);

  ASSERT_EQ(canvas_poc04_session_insert_utf8(session, "a", 1),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_insert_utf8(session, "\n", 1),
            CANVAS_POC04_STATUS_OK);

  canvas_poc04_utf16_range_t selection{};
  ASSERT_EQ(canvas_poc04_session_selection_flat_utf16(session, &selection),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(selection.location, 2U);
  EXPECT_EQ(selection.length, 0U);

  size_t required = 0;
  ASSERT_EQ(canvas_poc04_session_presented_text_range_utf8(
                session, 0, 2, nullptr, 0, &required),
            CANVAS_POC04_STATUS_BUFFER_TOO_SMALL);
  std::string text(required, '\0');
  ASSERT_EQ(canvas_poc04_session_presented_text_range_utf8(
                session, 0, 2, text.data(), text.size(), &required),
            CANVAS_POC04_STATUS_OK);
  text.resize(required - 1);
  EXPECT_EQ(text, "a\n");

  canvas_poc04_rect_t caret{};
  ASSERT_EQ(canvas_poc04_session_caret_rect_for_offset_utf16(
                session, selection.location, 320.0F, &caret),
            CANVAS_POC04_STATUS_OK);
  EXPECT_FLOAT_EQ(caret.x, 0.0F);
  EXPECT_FLOAT_EQ(caret.y, 20.0F);
  EXPECT_FLOAT_EQ(caret.height, 20.0F);
  EXPECT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
}

TEST(CApiTest, ConsecutiveNewlinesKeepCaretOnEachEmptyVisualLine) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  ASSERT_EQ(canvas_poc04_session_create(&info, &session),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_insert_utf8(session, "a\n\n\n", 4),
            CANVAS_POC04_STATUS_OK);

  canvas_poc04_utf16_range_t selection{};
  ASSERT_EQ(canvas_poc04_session_selection_flat_utf16(session, &selection),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(selection.location, 4U);
  EXPECT_EQ(selection.length, 0U);

  canvas_poc04_rect_t caret{};
  ASSERT_EQ(canvas_poc04_session_caret_rect_for_offset_utf16(
                session, selection.location, 320.0F, &caret),
            CANVAS_POC04_STATUS_OK);
  EXPECT_FLOAT_EQ(caret.x, 0.0F);
  EXPECT_FLOAT_EQ(caret.y, 60.0F);
  EXPECT_FLOAT_EQ(caret.height, 20.0F);
  EXPECT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
}

TEST(CApiTest, AppleMarkedSpacingKeepsCaretNearNativePreeditWidth) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  ASSERT_EQ(canvas_poc04_session_create(&info, &session),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);
  const std::string marked = "g\xE2\x80\x86g";  // g + U+2006 + g.
  ASSERT_EQ(canvas_poc04_session_insert_utf8(session, marked.data(), marked.size()),
            CANVAS_POC04_STATUS_OK);

  canvas_poc04_rect_t caret{};
  ASSERT_EQ(canvas_poc04_session_caret_rect_for_offset_utf16(
                session, 3, 320.0F, &caret),
            CANVAS_POC04_STATUS_OK);
  // U+2006 is deliberately narrow so the marked syllable separator does not
  // create the large gap produced by treating it as an ordinary space.
  EXPECT_FLOAT_EQ(caret.x, 21.55F);
  EXPECT_FLOAT_EQ(caret.y, 0.0F);
  EXPECT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
}

TEST(CApiTest, AsciiSpacesUseProportionalEditorAdvance) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  ASSERT_EQ(canvas_poc04_session_create(&info, &session),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_focus(session), CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_insert_utf8(session, "a  b", 4),
            CANVAS_POC04_STATUS_OK);

  canvas_poc04_rect_t before_spaces{};
  canvas_poc04_rect_t after_spaces{};
  ASSERT_EQ(canvas_poc04_session_caret_rect_for_offset_utf16(
                session, 1, 320.0F, &before_spaces),
            CANVAS_POC04_STATUS_OK);
  ASSERT_EQ(canvas_poc04_session_caret_rect_for_offset_utf16(
                session, 3, 320.0F, &after_spaces),
            CANVAS_POC04_STATUS_OK);
  EXPECT_FLOAT_EQ(after_spaces.x - before_spaces.x, 8.4F);
  EXPECT_FLOAT_EQ(after_spaces.y, before_spaces.y);

  uint64_t hit = 0;
  ASSERT_EQ(canvas_poc04_session_character_offset_for_point(
                session, after_spaces.x, after_spaces.y, 320.0F, &hit),
            CANVAS_POC04_STATUS_OK);
  EXPECT_EQ(hit, 3U);
  EXPECT_EQ(canvas_poc04_session_destroy(session), CANVAS_POC04_STATUS_OK);
}

}  // namespace
