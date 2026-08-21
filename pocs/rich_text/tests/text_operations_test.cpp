#include <gtest/gtest.h>

#include "canvas_poc04/rich_text.h"

namespace canvas::poc04 {
namespace {

TEST(TextOperationsTest, ReplayMatchesLiveDocument) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"English ");
  session.BeginComposition();
  session.UpdateComposition(u"中文", 2, 2);
  session.CommitComposition();
  session.InsertText(u"\nCanvas v2");

  TextDocument replayed;
  TextOperationEngine::ReplayNdjson(replayed, session.OperationLogNdjson());
  EXPECT_EQ(replayed.Digest(), document->Digest());
  EXPECT_EQ(replayed.PlainText(), document->PlainText());
}

TEST(TextOperationsTest, ReplayIsTransactional) {
  TextDocument document;
  TextStyle style;
  TextOperationEngine::Apply(
      document, {1, "seed", {{{{0, 0}, {0, 0}}, {u"seed", {style, style, style, style}}}}});
  const std::string before = document.Digest();
  const std::string invalid =
      "{\"v\":1,\"seq\":2,\"op\":\"text_transaction\",\"origin\":\"x\","
      "\"changes\":[{\"range\":{\"anchor\":{\"paragraph\":0,\"offset_utf16\":0},"
      "\"focus\":{\"paragraph\":0,\"offset_utf16\":0}},\"inserted\":{\"text\":\"x\","
      "\"styles_utf16\":[]}}]}\n";
  EXPECT_THROW(TextOperationEngine::ReplayNdjson(document, invalid),
               std::invalid_argument);
  EXPECT_EQ(document.Digest(), before);
}

TEST(TextOperationsTest, RejectsNonContiguousSequence) {
  TextDocument document;
  EXPECT_THROW(TextOperationEngine::Apply(
                   document, {2, "gap", {{{{0, 0}, {0, 0}}, {}}}}),
               std::invalid_argument);
}

}  // namespace
}  // namespace canvas::poc04
