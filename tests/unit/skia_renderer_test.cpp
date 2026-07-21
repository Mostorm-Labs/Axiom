#include "canvas/render/skia_renderer.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

#include <gtest/gtest.h>

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
