#include <gtest/gtest.h>

#include "canvas_poc04/rich_text.h"

namespace canvas::poc04 {
namespace {

TEST(TextDocumentTest, UtfRoundTripHandlesBmpAndSurrogatePairs) {
  const std::string utf8 = "Canvas 中文 😀";
  EXPECT_EQ(Utf16ToUtf8(Utf8ToUtf16(utf8)), utf8);
  EXPECT_THROW(Utf8ToUtf16("\xf0\x28\x8c\x28"), std::invalid_argument);
  EXPECT_THROW(Utf16ToUtf8(std::u16string(1, 0xd800)), std::invalid_argument);
}

TEST(TextDocumentTest, SnapshotPreservesRunsAttributesAndDigest) {
  TextDocument document;
  TextStyle red;
  red.rgba = 0xff0000ffU;
  TextStyle blue;
  blue.rgba = 0x0000ffffU;
  TextOperationEngine::Apply(
      document,
      {1, "fixture", {{{{0, 0}, {0, 0}},
                        {u"Red 蓝", {red, red, red, red, blue}}}}});
  const std::string digest = document.Digest();
  TextDocument restored = TextDocument::FromSnapshotJson(document.SnapshotJson());
  EXPECT_EQ(restored.Digest(), digest);
  ASSERT_EQ(restored.paragraphs().front().runs.size(), 2U);
  EXPECT_EQ(restored.PlainText(), u"Red 蓝");
}

TEST(TextDocumentTest, DigestIsStableForIdenticalOperationSequence) {
  auto build = [] {
    TextDocument document;
    TextStyle style;
    TextOperationEngine::Apply(document,
                               {1, "test", {{{{0, 0}, {0, 0}},
                                              {u"a\nb", {style, style, style}}}}});
    return document.Digest();
  };
  const std::string expected = build();
  for (int iteration = 0; iteration < 10; ++iteration) {
    EXPECT_EQ(build(), expected);
  }
}

TEST(TextDocumentTest, CanonicalFontIdentityAndFallbackAffectDigest) {
  TextDocument first;
  TextStyle style;
  TextOperationEngine::Apply(
      first, {1, "fixture", {{{{0, 0}, {0, 0}}, {u"中", {style}}}}});
  TextDocument second;
  style.fallback_chain.clear();
  TextOperationEngine::Apply(
      second, {1, "fixture", {{{{0, 0}, {0, 0}}, {u"中", {style}}}}});
  EXPECT_NE(first.Digest(), second.Digest());
}

}  // namespace
}  // namespace canvas::poc04
