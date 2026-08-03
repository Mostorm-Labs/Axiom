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

  ASSERT_TRUE(doc.appendStrokePoint("active", {{16, 8}, 1.0F, 3},
                                    {14, 6, 4, 4}));
  (void)renderer.renderRaster(doc, document::LayerClass::Annotation, 32, 32);

  EXPECT_EQ(renderer.fullPathBuildCount(), 1u);
  EXPECT_EQ(renderer.incrementalAppendCount(), 1u);
}

TEST(SkiaRendererTest, IncrementalAppendPreservesOldPathAndRendersNewTail) {
  document::Document doc;
  document::StrokeNode stroke;
  stroke.points = {{{4, 12}, 1.0F, 1}, {{12, 12}, 1.0F, 2}};
  stroke.width = 4.0F;
  stroke.colorArgb = 0xFF00FF00;
  ASSERT_TRUE(doc.add({"active", document::LayerClass::Annotation,
                       {2, 10, 12, 4}, {}, stroke}));

  render::SkiaRenderer renderer;
  const auto before =
      renderer.renderRaster(doc, document::LayerClass::Annotation, 32, 24);
  ASSERT_NE((before.pixel(8, 12) >> 24U) & 0xFFU, 0U);
  ASSERT_EQ(renderer.fullPathBuildCount(), 1u);

  ASSERT_TRUE(doc.appendStrokePoint("active", {{24, 12}, 1.0F, 3},
                                    {10, 10, 16, 4}));
  const auto after =
      renderer.renderRaster(doc, document::LayerClass::Annotation, 32, 24);

  EXPECT_NE((after.pixel(8, 12) >> 24U) & 0xFFU, 0U);
  EXPECT_NE((after.pixel(20, 12) >> 24U) & 0xFFU, 0U);
  EXPECT_EQ(renderer.fullPathBuildCount(), 1u);
  EXPECT_EQ(renderer.incrementalAppendCount(), 1u);
}

TEST(SkiaRendererTest, IncrementalAppendFromSinglePointDrawsFirstSegment) {
  document::Document doc;
  document::StrokeNode stroke;
  stroke.points = {{{20, 4}, 1.0F, 1}};
  stroke.width = 4.0F;
  stroke.colorArgb = 0xFF00FF00;
  ASSERT_TRUE(doc.add({"active", document::LayerClass::Annotation,
                       {18, 2, 4, 4}, {}, stroke}));

  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(doc, document::LayerClass::Annotation, 40, 28);
  ASSERT_EQ(renderer.fullPathBuildCount(), 1u);
  ASSERT_EQ(renderer.cachedChunkCount("active"), 1u);

  ASSERT_TRUE(doc.appendStrokePoint("active", {{20, 20}, 1.0F, 2},
                                    {18, 2, 4, 20}));
  const auto after =
      renderer.renderRaster(doc, document::LayerClass::Annotation, 40, 28);

  EXPECT_NE((after.pixel(20, 12) >> 24U) & 0xFFU, 0U);
  EXPECT_EQ(renderer.cachedChunkCount("active"), 1u);
  EXPECT_EQ(renderer.fullPathBuildCount(), 1u);
  EXPECT_EQ(renderer.incrementalAppendCount(), 1u);
}

TEST(SkiaRendererTest, IncrementalAppendStartsNewChunkAfterExactly64Segments) {
  document::Document doc;
  document::StrokeNode stroke;
  stroke.width = 4.0F;
  stroke.colorArgb = 0xFF00FF00;
  for (int index = 0; index <= 64; ++index) {
    stroke.points.push_back(
        {{static_cast<float>(index + 2), 12.0F}, 1.0F,
         static_cast<std::uint64_t>(index + 1)});
  }
  ASSERT_TRUE(doc.add({"boundary", document::LayerClass::Annotation,
                       {0, 10, 70, 4}, {}, stroke}));

  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(doc, document::LayerClass::Annotation, 96, 32);
  ASSERT_EQ(renderer.fullPathBuildCount(), 1u);
  ASSERT_EQ(renderer.cachedChunkCount("boundary"), 1u);

  ASSERT_TRUE(doc.appendStrokePoint("boundary", {{66, 20}, 1.0F, 66},
                                    {64, 10, 4, 12}));
  const auto after =
      renderer.renderRaster(doc, document::LayerClass::Annotation, 96, 32);

  EXPECT_NE((after.pixel(66, 16) >> 24U) & 0xFFU, 0U);
  EXPECT_EQ(renderer.cachedChunkCount("boundary"), 2u);
  EXPECT_EQ(renderer.fullPathBuildCount(), 1u);
  EXPECT_EQ(renderer.incrementalAppendCount(), 1u);
}

TEST(SkiaRendererTest, RebuildsWhenMiddlePointChangesAtSameCount) {
  document::Document doc;
  document::StrokeNode stroke;
  stroke.points = {{{2, 2}, 1.0F, 1},
                   {{8, 4}, 1.0F, 2},
                   {{16, 16}, 1.0F, 3}};
  ASSERT_TRUE(doc.add({"stroke", document::LayerClass::Base,
                       {0, 0, 20, 20}, {}, stroke}));
  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(doc, document::LayerClass::Base, 32, 32);
  ASSERT_TRUE(doc.mutate("stroke", [](document::Node& node) {
    std::get<document::StrokeNode>(node.payload).points[1].position.y = 12;
  }));
  (void)renderer.renderRaster(doc, document::LayerClass::Base, 32, 32);
  EXPECT_EQ(renderer.fullPathBuildCount(), 2u);
}

TEST(SkiaRendererTest, GenericMutationAppendingPointForcesRebuild) {
  document::Document doc;
  document::StrokeNode stroke;
  stroke.points = {{{2, 2}, 1.0F, 1}, {{8, 8}, 1.0F, 2}};
  ASSERT_TRUE(doc.add({"stroke", document::LayerClass::Base,
                       {0, 0, 12, 12}, {}, stroke}));
  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(doc, document::LayerClass::Base, 32, 32);
  ASSERT_TRUE(doc.mutate("stroke", [](document::Node& node) {
    std::get<document::StrokeNode>(node.payload).points.push_back(
        {{16, 16}, 1.0F, 3});
  }));
  (void)renderer.renderRaster(doc, document::LayerClass::Base, 32, 32);
  EXPECT_EQ(renderer.fullPathBuildCount(), 2u);
}

TEST(SkiaRendererTest, DirtyCullSkipsChangedStrokeOutsideBounds) {
  document::Document doc;
  document::StrokeNode nearStroke;
  nearStroke.points = {{{2, 2}, 1.0F, 1}, {{8, 8}, 1.0F, 2}};
  document::StrokeNode farStroke;
  farStroke.points = {{{50, 50}, 1.0F, 1},
                      {{56, 54}, 1.0F, 2},
                      {{62, 62}, 1.0F, 3}};
  ASSERT_TRUE(doc.add({"near", document::LayerClass::Base,
                       {0, 0, 12, 12}, {}, nearStroke}));
  ASSERT_TRUE(doc.add({"far", document::LayerClass::Base,
                       {48, 48, 16, 16}, {}, farStroke}));
  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(doc, document::LayerClass::Base, 80, 80);
  ASSERT_EQ(renderer.fullPathBuildCount(), 2u);
  ASSERT_TRUE(doc.mutate("far", [](document::Node& node) {
    std::get<document::StrokeNode>(node.payload).points[1].position.y = 58;
  }));
  auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(80, 80));
  ASSERT_TRUE(static_cast<bool>(surface));
  renderer.drawLayer(*surface->getCanvas(), doc, document::LayerClass::Base,
                     core::Rect{0, 0, 16, 16});
  EXPECT_EQ(renderer.fullPathBuildCount(), 2u);
}

TEST(SkiaRendererTest, ParentNormalizedAppendUsesCachedWorldPath) {
  document::Document doc;
  ASSERT_TRUE(doc.add({"parent", document::LayerClass::Embedded,
                       {10, 10, 40, 40}, {}, document::EmbeddedNode{}}));
  document::StrokeNode stroke;
  stroke.coordinateSpace = document::StrokeCoordinateSpace::ParentNormalized;
  stroke.width = 0.1F;
  stroke.points = {{{0.1F, 0.2F}, 1.0F, 1}, {{0.8F, 0.8F}, 1.0F, 2}};
  ASSERT_TRUE(doc.add({"attached", document::LayerClass::Annotation,
                       {10, 10, 40, 40}, "parent", stroke}));
  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(doc, document::LayerClass::Annotation, 80, 80);
  ASSERT_EQ(renderer.fullPathBuildCount(), 1u);
  ASSERT_TRUE(doc.appendStrokePoint("attached", {{0.9F, 0.1F}, 1.0F, 3},
                                    {0, 0, 0, 0}));
  (void)renderer.renderRaster(doc, document::LayerClass::Annotation, 80, 80);
  EXPECT_EQ(renderer.fullPathBuildCount(), 1u);
  EXPECT_EQ(renderer.incrementalAppendCount(), 1u);
  ASSERT_TRUE(doc.setBounds("parent", {20, 20, 80, 40}));
  (void)renderer.renderRaster(doc, document::LayerClass::Annotation, 128, 128);
  EXPECT_EQ(renderer.fullPathBuildCount(), 2u);
}

TEST(SkiaRendererTest, UnrelatedNodeAddPreservesExistingPathCache) {
  document::Document doc;
  document::StrokeNode first;
  first.points = {{{2, 2}, 1.0F, 1}, {{8, 8}, 1.0F, 2}};
  ASSERT_TRUE(doc.add({"first", document::LayerClass::Base,
                       {0, 0, 10, 10}, {}, first}));
  render::SkiaRenderer renderer;
  (void)renderer.renderRaster(doc, document::LayerClass::Base, 32, 32);
  ASSERT_EQ(renderer.fullPathBuildCount(), 1u);
  document::StrokeNode unrelated;
  unrelated.points = {{{20, 20}, 1.0F, 1}, {{24, 24}, 1.0F, 2}};
  ASSERT_TRUE(doc.add({"unrelated", document::LayerClass::Base,
                       {18, 18, 10, 10}, {}, unrelated}));
  (void)renderer.renderRaster(doc, document::LayerClass::Base, 32, 32);
  EXPECT_EQ(renderer.fullPathBuildCount(), 2u);
}

TEST(SkiaRendererTest, LongStrokeDrawsOnlyDirtyTailChunks) {
  document::Document doc;
  document::StrokeNode stroke;
  stroke.width = 4.0F;
  for (int x = 1; x <= 140; ++x) {
    stroke.points.push_back(
        {{static_cast<float>(x), 16.0F}, 1.0F,
         static_cast<std::uint64_t>(x)});
  }
  ASSERT_TRUE(doc.add({"long", document::LayerClass::Annotation,
                       {0, 12, 144, 8}, {}, stroke}));
  render::SkiaRenderer renderer;
  const auto full =
      renderer.renderRaster(doc, document::LayerClass::Annotation, 160, 32);
  ASSERT_GE(renderer.cachedChunkCount("long"), 3u);
  EXPECT_NE((full.pixel(65, 16) >> 24U) & 0xFFU, 0U);

  ASSERT_TRUE(doc.appendStrokePoint("long", {{145, 16}, 1.0F, 141},
                                    {140, 12, 8, 8}));
  auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(160, 32));
  ASSERT_TRUE(static_cast<bool>(surface));
  renderer.drawLayer(*surface->getCanvas(), doc,
                     document::LayerClass::Annotation,
                     core::Rect{140, 12, 8, 8});
  EXPECT_EQ(renderer.incrementalAppendCount(), 1u);
  EXPECT_LE(renderer.lastDrawnChunkCount(), 2u);
  EXPECT_LT(renderer.lastDrawnChunkCount(), renderer.cachedChunkCount("long"));
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
  // Use multi-pixel resolved widths so integer alpha bounds can observe the
  // resize consistently across Skia raster backends (2 px before, 4 px after).
  attached.width = 0.1F;
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
