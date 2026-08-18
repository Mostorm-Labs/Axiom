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

}  // namespace
