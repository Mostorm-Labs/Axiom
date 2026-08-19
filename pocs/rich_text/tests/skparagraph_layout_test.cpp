#include <algorithm>
#include <stdexcept>

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

TEST(SkParagraphLayoutTest, PerformanceLayoutRebuildsForWidthAndRevision) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(std::u16string(160, u'a'));
  SkParagraphTextLayout layout(CANVAS_POC04_FONT_PATH,
                               CANVAS_POC04_CJK_FONT_PATH);

  const auto wide = layout.LayoutForPerformance(*document, 320.0F);
  const auto narrow = layout.LayoutForPerformance(*document, 32.0F);
  EXPECT_FLOAT_EQ(wide.width, 320.0F);
  EXPECT_FLOAT_EQ(narrow.width, 32.0F);
  EXPECT_GT(narrow.height, wide.height);
  EXPECT_TRUE(wide.lines.empty());
  EXPECT_TRUE(wide.clusters.empty());
  EXPECT_TRUE(wide.selection_rects.empty());

  session.SetSelection({{0, 160}, {0, 160}});
  session.InsertText(std::u16string(160, u'a'));
  const auto revised = layout.LayoutForPerformance(*document, 320.0F);
  EXPECT_GT(revised.height, wide.height);
  EXPECT_TRUE(revised.diagnostics.empty());
}

TEST(SkParagraphLayoutTest, PerformanceLayoutReportsUnresolvedGlyphs) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"中文");
  SkParagraphTextLayout layout(CANVAS_POC04_FONT_PATH,
                               CANVAS_POC04_CJK_FONT_PATH);

  const auto result = layout.LayoutForPerformance(*document, 320.0F);
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics.front(), "unresolved-glyphs");
}

TEST(SkParagraphLayoutTest, PerformanceLayoutRejectsInvalidWidth) {
  auto document = std::make_shared<TextDocument>();
  SkParagraphTextLayout layout(CANVAS_POC04_FONT_PATH,
                               CANVAS_POC04_CJK_FONT_PATH);
  EXPECT_THROW(layout.LayoutForPerformance(*document, 0.0F),
               std::invalid_argument);
}

TEST(SkParagraphLayoutTest, RejectsFontFixtureWhoseBytesDoNotMatchIdentity) {
  EXPECT_THROW(SkParagraphTextLayout(CANVAS_POC04_CJK_FONT_PATH,
                                    CANVAS_POC04_CJK_FONT_PATH),
               std::runtime_error);
}
}  // namespace canvas::poc04
