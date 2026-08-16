#include "software_probe_renderer.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace canvas::poc01 {

void SoftwareProbeRenderer::BlendPixel(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= static_cast<int>(width_) ||
      y >= static_cast<int>(height_)) {
    return;
  }
  uint8_t* destination =
      pixels_.data() + (static_cast<size_t>(y) * width_ + x) * 4U;
  const uint32_t alpha = color.a;
  const uint32_t inverse = 255U - alpha;
  destination[0] = static_cast<uint8_t>(
      (static_cast<uint32_t>(color.r) * alpha + destination[0] * inverse +
       127U) /
      255U);
  destination[1] = static_cast<uint8_t>(
      (static_cast<uint32_t>(color.g) * alpha + destination[1] * inverse +
       127U) /
      255U);
  destination[2] = static_cast<uint8_t>(
      (static_cast<uint32_t>(color.b) * alpha + destination[2] * inverse +
       127U) /
      255U);
  destination[3] = 255;
}

void SoftwareProbeRenderer::FillRect(float x, float y, float width,
                                     float height, Color color) {
  const int left = static_cast<int>(std::floor(x));
  const int top = static_cast<int>(std::floor(y));
  const int right = static_cast<int>(std::ceil(x + width));
  const int bottom = static_cast<int>(std::ceil(y + height));
  for (int row = top; row < bottom; ++row) {
    for (int column = left; column < right; ++column) {
      BlendPixel(column, row, color);
    }
  }
}

void SoftwareProbeRenderer::DrawLine(float x0, float y0, float x1, float y1,
                                     float width, Color color) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const int steps = std::max(1, static_cast<int>(std::ceil(
                                    std::max(std::abs(dx), std::abs(dy)))));
  const float radius = std::max(0.5F, width * 0.5F);
  for (int step = 0; step <= steps; ++step) {
    const float ratio = static_cast<float>(step) / steps;
    const float x = x0 + dx * ratio;
    const float y = y0 + dy * ratio;
    const int min_x = static_cast<int>(std::floor(x - radius));
    const int max_x = static_cast<int>(std::ceil(x + radius));
    const int min_y = static_cast<int>(std::floor(y - radius));
    const int max_y = static_cast<int>(std::ceil(y + radius));
    for (int py = min_y; py <= max_y; ++py) {
      for (int px = min_x; px <= max_x; ++px) {
        const float offset_x = static_cast<float>(px) + 0.5F - x;
        const float offset_y = static_cast<float>(py) + 0.5F - y;
        if (offset_x * offset_x + offset_y * offset_y <= radius * radius) {
          BlendPixel(px, py, color);
        }
      }
    }
  }
}

canvas_poc_status_t SoftwareProbeRenderer::Render(
    const RuntimeScene& scene, const AssetRegistry& assets, uint32_t width,
    uint32_t height, float device_pixel_ratio) {
  if (width == 0 || height == 0 || !IsFinite(device_pixel_ratio) ||
      device_pixel_ratio <= 0.0F || device_pixel_ratio != 1.0F) {
    SetLastError("POC-01 probe renderer requires positive dimensions and DPR 1");
    return CANVAS_POC_STATUS_RENDER_ERROR;
  }
  if (static_cast<uint64_t>(width) * height > 100000000ULL) {
    SetLastError("probe renderer dimensions exceed safety limit");
    return CANVAS_POC_STATUS_RENDER_ERROR;
  }
  width_ = width;
  height_ = height;
  pixels_.resize(static_cast<size_t>(width) * height * 4U);
  for (size_t offset = 0; offset < pixels_.size(); offset += 4) {
    pixels_[offset] = scene.background.r;
    pixels_[offset + 1] = scene.background.g;
    pixels_[offset + 2] = scene.background.b;
    pixels_[offset + 3] = scene.background.a;
  }

  for (const Node& node : scene.draw_items) {
    std::visit(
        [this, &assets](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          const float tx = value.header.translation_x;
          const float ty = value.header.translation_y;
          if constexpr (std::is_same_v<T, RectNode>) {
            FillRect(value.x + tx, value.y + ty, value.width, value.height,
                     value.color);
          } else if constexpr (std::is_same_v<T, ImageNode>) {
            const Asset* asset = assets.Find(value.asset_key);
            if (asset == nullptr) {
              return;
            }
            const Color first{32, 95, 210, 255};
            const Color second{245, 178, 45, 255};
            const int left = static_cast<int>(std::floor(value.x + tx));
            const int top = static_cast<int>(std::floor(value.y + ty));
            const int right = static_cast<int>(
                std::ceil(value.x + tx + value.width));
            const int bottom = static_cast<int>(
                std::ceil(value.y + ty + value.height));
            for (int y = top; y < bottom; ++y) {
              for (int x = left; x < right; ++x) {
                const int local_x = x - left;
                const int local_y = y - top;
                BlendPixel(x, y, ((local_x / 8 + local_y / 8) % 2 == 0)
                                     ? first
                                     : second);
              }
            }
          } else if constexpr (std::is_same_v<T, VectorPathNode>) {
            float current_x = 0.0F;
            float current_y = 0.0F;
            float start_x = 0.0F;
            float start_y = 0.0F;
            for (const PathCommand& command : value.commands) {
              if (command.verb == PathVerb::kMove) {
                current_x = command.points[0] + tx;
                current_y = command.points[1] + ty;
                start_x = current_x;
                start_y = current_y;
              } else if (command.verb == PathVerb::kLine) {
                const float next_x = command.points[0] + tx;
                const float next_y = command.points[1] + ty;
                DrawLine(current_x, current_y, next_x, next_y,
                         value.stroke_width, value.color);
                current_x = next_x;
                current_y = next_y;
              } else if (command.verb == PathVerb::kCubic) {
                const float origin_x = current_x;
                const float origin_y = current_y;
                for (int segment = 1; segment <= 32; ++segment) {
                  const float t = static_cast<float>(segment) / 32.0F;
                  const float u = 1.0F - t;
                  const float next_x =
                      u * u * u * origin_x +
                      3.0F * u * u * t * (command.points[0] + tx) +
                      3.0F * u * t * t * (command.points[2] + tx) +
                      t * t * t * (command.points[4] + tx);
                  const float next_y =
                      u * u * u * origin_y +
                      3.0F * u * u * t * (command.points[1] + ty) +
                      3.0F * u * t * t * (command.points[3] + ty) +
                      t * t * t * (command.points[5] + ty);
                  DrawLine(current_x, current_y, next_x, next_y,
                           value.stroke_width, value.color);
                  current_x = next_x;
                  current_y = next_y;
                }
              } else if (command.verb == PathVerb::kClose) {
                DrawLine(current_x, current_y, start_x, start_y,
                         value.stroke_width, value.color);
                current_x = start_x;
                current_y = start_y;
              }
            }
          } else if constexpr (std::is_same_v<T, TextNode>) {
            const float glyph_width = value.font_size * 0.55F;
            const float glyph_height = value.font_size;
            float cursor = value.x + tx;
            for (unsigned char character : value.text) {
              if (character != ' ') {
                const float inset =
                    static_cast<float>((character % 3U) + 1U);
                FillRect(cursor + inset, value.y + ty - glyph_height + inset,
                         std::max(1.0F, glyph_width - inset * 2.0F),
                         std::max(1.0F, glyph_height - inset * 2.0F),
                         value.color);
              }
              cursor += glyph_width;
            }
          }
        },
        node);
  }
  return CANVAS_POC_STATUS_OK;
}

}  // namespace canvas::poc01
