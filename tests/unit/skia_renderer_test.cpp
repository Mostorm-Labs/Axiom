#include "canvas/render/skia_renderer.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

using namespace canvas;

TEST(SkiaRendererTest, SeparatesBaseAndAnnotationPixels) {
  document::Document doc;
  document::StrokeNode baseStroke;
  baseStroke.points = {{{10, 10}, 1.0F, 1}, {{40, 10}, 1.0F, 2}};
  baseStroke.width = 6;
  baseStroke.colorArgb = 0xFFFF0000;
  ASSERT_TRUE(doc.add({"base", document::LayerClass::Base, {7, 7, 36, 6}, {},
                       baseStroke}));

  document::StrokeNode annotationStroke;
  annotationStroke.points = {{{10, 30}, 1.0F, 1}, {{40, 30}, 1.0F, 2}};
  annotationStroke.width = 6;
  annotationStroke.colorArgb = 0xFF0000FF;
  ASSERT_TRUE(doc.add({"annotation", document::LayerClass::Annotation,
                       {7, 27, 36, 6}, {}, annotationStroke}));

  render::SkiaRenderer renderer;
  const auto base =
      renderer.renderRaster(doc, document::LayerClass::Base, 64, 64);
  const auto annotation =
      renderer.renderRaster(doc, document::LayerClass::Annotation, 64, 64);

  EXPECT_NE((base.pixel(20, 10) >> 24U) & 0xFFU, 0U);
  EXPECT_EQ((base.pixel(20, 30) >> 24U) & 0xFFU, 0U);
  EXPECT_NE((annotation.pixel(20, 30) >> 24U) & 0xFFU, 0U);
  EXPECT_EQ((annotation.pixel(20, 10) >> 24U) & 0xFFU, 0U);
}

TEST(SkiaRendererTest, SkipsEmptyAndNonStrokePayloads) {
  document::Document doc;
  document::StrokeNode emptyStroke;
  ASSERT_TRUE(doc.add({"empty", document::LayerClass::Base, {}, {},
                       emptyStroke}));
  document::EmbeddedNode embedded;
  ASSERT_TRUE(doc.add({"embedded", document::LayerClass::Base, {}, {},
                       embedded}));

  render::SkiaRenderer renderer;
  const auto frame =
      renderer.renderRaster(doc, document::LayerClass::Base, 16, 16);
  ASSERT_EQ(frame.bgraPremultiplied.size(), 16U * 16U);
  for (const auto pixel : frame.bgraPremultiplied) {
    EXPECT_EQ(pixel, 0U);
  }
}

TEST(SkiaRendererTest, DirtyClipConfinesRasterOutput) {
  document::Document doc;
  document::StrokeNode stroke;
  stroke.points = {{{2, 8}, 1.0F, 1}, {{14, 8}, 1.0F, 2}};
  stroke.width = 4;
  stroke.colorArgb = 0xFF00FF00;
  ASSERT_TRUE(doc.add({"stroke", document::LayerClass::Base, {0, 6, 16, 4},
                       {}, stroke}));

  render::SkiaRenderer renderer;
  auto surface = SkSurfaces::Raster(
      SkImageInfo::MakeN32Premul(16, 16));
  ASSERT_TRUE(static_cast<bool>(surface));
  surface->getCanvas()->clear(SK_ColorTRANSPARENT);
  renderer.drawLayer(*surface->getCanvas(), doc, document::LayerClass::Base,
                     core::Rect{0, 0, 8, 16});

  std::vector<std::uint32_t> pixels(16U * 16U);
  ASSERT_TRUE(surface->readPixels(
      SkImageInfo::MakeN32Premul(16, 16), pixels.data(), 16U * sizeof(std::uint32_t),
      0, 0));
  EXPECT_NE((pixels[8 * 16 + 4] >> 24U) & 0xFFU, 0U);
  EXPECT_EQ((pixels[8 * 16 + 12] >> 24U) & 0xFFU, 0U);
}

TEST(SkiaRendererTest, AppendsActiveStrokePathWithoutFullRebuild) {
  document::Document doc;
  document::StrokeNode stroke;
  stroke.points = {{{2, 8}, 1.0F, 1}, {{8, 8}, 1.0F, 2}};
  ASSERT_TRUE(doc.add({"active", document::LayerClass::Annotation,
                       {0, 0, 16, 16}, {}, stroke}));
  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(doc, document::LayerClass::Annotation, 32, 32);
  ASSERT_EQ(renderer.fullPathBuildCount(), 1u);

  auto* node = doc.find("active");
  ASSERT_NE(node, nullptr);
  std::get<document::StrokeNode>(node->payload)
      .points.push_back({{16, 8}, 1.0F, 3});
  (void)renderer.renderRaster(doc, document::LayerClass::Annotation, 32, 32);

  EXPECT_EQ(renderer.fullPathBuildCount(), 1u);
  EXPECT_EQ(renderer.incrementalAppendCount(), 1u);
}

TEST(SkiaRendererTest, DoesNotReuseSameNodeIdAcrossDocuments) {
  auto makeDocument = [](float middleY) {
    document::Document doc;
    document::StrokeNode stroke;
    stroke.points = {{{2, 2}, 1.0F, 1},
                     {{8, middleY}, 1.0F, 2},
                     {{16, 16}, 1.0F, 3}};
    EXPECT_TRUE(doc.add({"shared-id", document::LayerClass::Base,
                         {0, 0, 20, 20}, {}, stroke}));
    return doc;
  };
  auto first = makeDocument(4.0F);
  auto second = makeDocument(12.0F);
  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(first, document::LayerClass::Base, 32, 32);
  (void)renderer.renderRaster(second, document::LayerClass::Base, 32, 32);
  EXPECT_EQ(renderer.fullPathBuildCount(), 2u);
}

TEST(SkiaRendererTest, ResolvesParentNormalizedStrokeAfterParentMoveAndResize) {
  document::Document doc;
  document::Node parent{"embed", document::LayerClass::Embedded,
                        {10, 10, 40, 20}, {}, document::EmbeddedNode{}};
  ASSERT_TRUE(doc.add(parent));

  document::StrokeNode attached;
  attached.points = {{{0.25F, 0.5F}, 1.0F, 1},
                     {{0.75F, 0.5F}, 1.0F, 2}};
  attached.width = 0.02F;
  attached.coordinateSpace = document::StrokeCoordinateSpace::ParentNormalized;
  attached.colorArgb = 0xFFFF0000;
  ASSERT_TRUE(doc.add({"annotation", document::LayerClass::Annotation,
                       {0, 0, 0, 0}, "embed", attached}));

  render::SkiaRenderer renderer;
  const auto before =
      renderer.renderRaster(doc, document::LayerClass::Annotation, 128, 128);
  ASSERT_TRUE(doc.setBounds("embed", {20, 20, 80, 40}));
  const auto after =
      renderer.renderRaster(doc, document::LayerClass::Annotation, 128, 128);

  auto alphaBounds = [](const render::RasterFrame& frame) {
    int minX = frame.width;
    int minY = frame.height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < frame.height; ++y) {
      for (int x = 0; x < frame.width; ++x) {
        if (((frame.pixel(x, y) >> 24U) & 0xFFU) == 0U) {
          continue;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
      }
    }
    return std::array<int, 4>{minX, minY, maxX, maxY};
  };

  const auto beforeBounds = alphaBounds(before);
  const auto afterBounds = alphaBounds(after);
  EXPECT_LT(beforeBounds[0], 25);
  EXPECT_LT(beforeBounds[1], 20);
  EXPECT_GT(beforeBounds[2], 35);
  EXPECT_GT(afterBounds[0], beforeBounds[0] + 10);
  EXPECT_GT(afterBounds[1], beforeBounds[1] + 10);
  EXPECT_GT(afterBounds[2] - afterBounds[0],
            beforeBounds[2] - beforeBounds[0]);
  EXPECT_GT(afterBounds[3] - afterBounds[1],
            beforeBounds[3] - beforeBounds[1]);
}
