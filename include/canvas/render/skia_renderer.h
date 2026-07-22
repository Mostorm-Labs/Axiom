#pragma once

#include "canvas/render/renderer.h"
#include "include/core/SkPath.h"

#include <optional>
#include <string_view>
#include <unordered_map>

class SkCanvas;

namespace canvas::render {

class SkiaRenderer final : public Renderer {
 public:
  RasterFrame renderRaster(const document::Document& document,
                           document::LayerClass layer, int width,
                           int height) override;

  void drawLayer(
      SkCanvas& canvas, const document::Document& document,
      document::LayerClass layer,
      const std::optional<core::Rect>& dirtyBounds = std::nullopt);
  void invalidateNode(std::string_view id) {
    pathCache_.erase(document::NodeId{id});
  }
  std::size_t fullPathBuildCount() const noexcept {
    return fullPathBuildCount_;
  }
  std::size_t incrementalAppendCount() const noexcept {
    return incrementalAppendCount_;
  }

 private:
  struct CachedStroke {
    const document::StrokeNode* source = nullptr;
    std::size_t pointCount = 0;
    core::Vec2 firstPoint{};
    core::Vec2 lastPoint{};
    float width = 0.0F;
    std::uint32_t colorArgb = 0;
    document::StrokeCoordinateSpace coordinateSpace =
        document::StrokeCoordinateSpace::World;
    core::Rect parentBounds{};
    SkPath path;
  };

  std::unordered_map<document::NodeId, CachedStroke> pathCache_;
  std::size_t fullPathBuildCount_ = 0;
  std::size_t incrementalAppendCount_ = 0;
};

}  // namespace canvas::render
