#include <gtest/gtest.h>

#include "operations.h"
#include "scene_compiler.h"
#include "test_fixture.h"

namespace canvas::poc01::test {
namespace {

TEST(SceneCompilerTest, RebuildPreservesOrderGeometryAndRevision) {
  auto document = MakeDocument();
  ASSERT_EQ(ApplyOperations(*document, FixedReplay()), CANVAS_POC_STATUS_OK);
  const RuntimeScene scene = SceneCompiler().Compile(*document);
  ASSERT_EQ(scene.draw_items.size(), 4U);
  EXPECT_EQ(scene.source_revision, document->state().revision);
  EXPECT_EQ(Header(scene.draw_items[0]).id, 1U);
  EXPECT_EQ(Header(scene.draw_items[1]).id, 2U);
  EXPECT_EQ(Header(scene.draw_items[2]).id, 3U);
  EXPECT_EQ(Header(scene.draw_items[3]).id, 4U);
  const RectNode& rect = std::get<RectNode>(scene.draw_items[0]);
  EXPECT_FLOAT_EQ(rect.x, 80.0F);
  EXPECT_FLOAT_EQ(rect.header.translation_x, 12.0F);
  EXPECT_FLOAT_EQ(rect.header.translation_y, 8.0F);
}

}  // namespace
}  // namespace canvas::poc01::test
