#include <gtest/gtest.h>

#include "canvas_poc04/rich_text.h"

namespace canvas::poc04 {
namespace {

TEST(TextLayoutTest, ProbeLayoutReturnsLinesClustersAndSelection) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"abc中文\nxyz");
  DeterministicTextLayout layout;
  const TextLayoutResult result =
      layout.Layout(*document, 200.0F, {{0, 1}, {0, 4}});
  EXPECT_EQ(result.lines.size(), 2U);
  EXPECT_EQ(result.clusters.size(), 8U);
  EXPECT_EQ(result.selection_rects.size(), 3U);
  EXPECT_FLOAT_EQ(result.height, 40.0F);
}

TEST(TextLayoutTest, WrapIsDeterministic) {
  auto document = std::make_shared<TextDocument>();
  TextEditSession session(document);
  session.Focus();
  session.InsertText(u"abcdefghij");
  DeterministicTextLayout layout;
  const auto first = layout.Layout(*document, 30.0F, {{0, 0}, {0, 0}});
  const auto second = layout.Layout(*document, 30.0F, {{0, 0}, {0, 0}});
  ASSERT_EQ(first.lines.size(), second.lines.size());
  for (size_t line = 0; line < first.lines.size(); ++line) {
    EXPECT_EQ(first.lines[line].start, second.lines[line].start);
    EXPECT_EQ(first.lines[line].end, second.lines[line].end);
    EXPECT_FLOAT_EQ(first.lines[line].width, second.lines[line].width);
  }
}

}  // namespace
}  // namespace canvas::poc04
