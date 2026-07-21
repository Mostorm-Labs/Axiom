#pragma once

#include "canvas/document/document.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace canvas::render {

struct RasterFrame {
  int width = 0;
  int height = 0;
  std::vector<std::uint32_t> bgraPremultiplied;

  std::uint32_t pixel(int x, int y) const {
    return bgraPremultiplied.at(
        static_cast<std::size_t>(y * width + x));
  }
};

class Renderer {
 public:
  virtual ~Renderer() = default;
  virtual RasterFrame renderRaster(const document::Document& document,
                                   document::LayerClass layer, int width,
                                   int height) = 0;
};

}  // namespace canvas::render
