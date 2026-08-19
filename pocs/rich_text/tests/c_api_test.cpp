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

}  // namespace
