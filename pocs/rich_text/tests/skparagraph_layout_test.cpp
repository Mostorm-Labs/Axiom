#include <algorithm>

#include <gtest/gtest.h>

#include "canvas_poc04/rich_text.h"
#include "skparagraph_layout.h"

namespace canvas::poc04 {
TEST(SkParagraphLayoutTest, FixedFontProducesLineAndSelectionGeometry) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"Canvas v2 是\nEnglish");
  SkParagraphTextLayout layout(CANVAS_POC04_FONT_PATH,
                               CANVAS_POC04_CJK_FONT_PATH);
  const auto result = layout.Layout(*document, 320.0F, {{0, 0}, {0, 6}});
  EXPECT_EQ(result.lines.size(), 2U);
  EXPECT_FALSE(result.selection_rects.empty());
  const auto cjk = std::find_if(
      result.clusters.begin(), result.clusters.end(),
      [](const ClusterGeometry& cluster) {
        return cluster.range == TextRange{{0, 10}, {0, 11}};
      });
  EXPECT_NE(cjk, result.clusters.end());
  EXPECT_TRUE(result.diagnostics.empty());
}

TEST(SkParagraphLayoutTest, RejectsFontFixtureWhoseBytesDoNotMatchIdentity) {
  EXPECT_THROW(SkParagraphTextLayout(CANVAS_POC04_CJK_FONT_PATH,
                                    CANVAS_POC04_CJK_FONT_PATH),
               std::runtime_error);
}
}  // namespace canvas::poc04
