#include <gtest/gtest.h>

#include "canvas_poc04/rich_text.h"

namespace canvas::poc04 {
namespace {

TEST(TextEditSessionTest, CompositionCancelDoesNotMutateOrLog) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  const std::string digest = document->Digest();
  session.BeginComposition();
  session.UpdateComposition(u"zhongwen", 8, 8);
  session.CancelComposition();
  EXPECT_EQ(document->Digest(), digest);
  EXPECT_TRUE(session.operation_log().empty());
}

TEST(TextEditSessionTest, CompositionCommitIsOneReplayableTransaction) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.BeginComposition();
  session.UpdateComposition(u"中文", 2, 2);
  session.CommitComposition();
  ASSERT_EQ(session.operation_log().size(), 1U);
  EXPECT_EQ(session.operation_log().front().origin, "ime-commit");
  EXPECT_EQ(document->PlainText(), u"中文");
}

TEST(TextEditSessionTest, SelectionClipboardUndoAndRedoUseOperations) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"Canvas v2");
  session.SetSelection({{0, 0}, {0, 6}});
  EXPECT_EQ(session.CopySelection(), u"Canvas");
  session.CutSelection();
  EXPECT_EQ(document->PlainText(), u" v2");
  EXPECT_TRUE(session.Undo());
  EXPECT_EQ(document->PlainText(), u"Canvas v2");
  EXPECT_TRUE(session.Redo());
  EXPECT_EQ(document->PlainText(), u" v2");
  EXPECT_EQ(session.operation_log().size(), 4U);
}

TEST(TextEditSessionTest, BlurAndDestructionDiscardComposition) {
  for (int iteration = 0; iteration < 100; ++iteration) {
    auto document = std::make_shared<TextDocument>();
    TextEditSession session(document);
    session.Focus();
    session.BeginComposition();
    session.UpdateComposition(u"preview", 7, 7);
    session.Blur();
    EXPECT_FALSE(session.composition().has_value());
    EXPECT_FALSE(session.focused());
    EXPECT_TRUE(session.operation_log().empty());
  }
}

TEST(TextEditSessionTest, SurroundingTextIsBoundedAroundCaret) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"0123456789");
  session.SetSelection({{0, 5}, {0, 5}});
  EXPECT_EQ(session.SurroundingText(4), u"3456");
  EXPECT_EQ(session.TextBeforeCursor(3), u"234");
  EXPECT_EQ(session.TextAfterCursor(3), u"567");
}

TEST(TextEditSessionTest, DirectionalDeletionIsOneUndoableOperation) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"abc\ndef");
  session.SetSelection({{1, 1}, {1, 1}});
  session.DeleteSurroundingText(2, 1);
  EXPECT_EQ(document->PlainText(), u"abcf");
  ASSERT_EQ(session.operation_log().size(), 2U);
  EXPECT_EQ(session.operation_log().back().origin, "delete-surrounding");
  EXPECT_TRUE(session.Undo());
  EXPECT_EQ(document->PlainText(), u"abc\ndef");
}

TEST(TextEditSessionTest, DirectionalDeletionDoesNotSplitSurrogatePairs) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"a😀b");
  session.SetSelection({{0, 3}, {0, 3}});
  session.DeleteSurroundingText(1, 0);
  EXPECT_EQ(document->PlainText(), u"ab");
}

TEST(TextEditSessionTest, QueriesAndCompositionSelectionDoNotSplitSurrogatePairs) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"a😀b😀c");
  session.SetSelection({{0, 3}, {0, 3}});
  EXPECT_EQ(session.TextBeforeCursor(1), u"");
  EXPECT_EQ(session.TextAfterCursor(2), u"b");
  EXPECT_NO_THROW(Utf16ToUtf8(session.SurroundingText(3)));

  session.BeginComposition();
  EXPECT_THROW(session.UpdateComposition(u"😀", 1, 1),
               std::invalid_argument);
  session.UpdateComposition(u"😀", 0, 2);
}

}  // namespace
}  // namespace canvas::poc04
