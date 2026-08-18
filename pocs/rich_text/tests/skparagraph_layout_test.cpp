#include <algorithm>

#include <gtest/gtest.h>

#include "canvas_poc04/rich_text.h"
#include "skparagraph_layout.h"

namespace canvas::poc04 {
TEST(SkParagraphLayoutTest, FixedFontProducesLineAndSelectionGeometry) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"Canvas v2 中文\nEnglish 😀");
  SkParagraphTextLayout layout(CANVAS_POC04_FONT_PATH,
                               CANVAS_POC04_CJK_FONT_PATH);
  const auto result = layout.Layout(*document, 320.0F, {{0, 0}, {0, 6}});
  EXPECT_EQ(result.lines.size(), 2U);
  EXPECT_FALSE(result.selection_rects.empty());
  EXPECT_TRUE(std::ranges::any_of(
      result.clusters, [](const ClusterGeometry& cluster) {
        return cluster.range.focus.offset_utf16 -
                   cluster.range.anchor.offset_utf16 ==
               2;
      }));
  EXPECT_TRUE(result.diagnostics.empty());
}

TEST(SkParagraphLayoutTest, RejectsFontFixtureWhoseBytesDoNotMatchIdentity) {
  EXPECT_THROW(SkParagraphTextLayout(CANVAS_POC04_CJK_FONT_PATH,
                                    CANVAS_POC04_CJK_FONT_PATH),
               std::runtime_error);
}
}  // namespace canvas::poc04
