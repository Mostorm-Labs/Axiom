#include "canvas_poc04/rich_text.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace canvas::poc04 {
namespace {

float UnitAdvance(char16_t unit) {
  if (unit >= 0xd800 && unit <= 0xdfff) {
    return 0.5F;
  }
  if (unit >= 0x2e80 || unit == 0xfffc) {
    return 1.0F;
  }
  return 0.6F;
}

LogicalPosition Advance(LogicalPosition position, char16_t unit) {
  if (unit == u'\n') {
    ++position.paragraph;
    position.offset_utf16 = 0;
  } else {
    ++position.offset_utf16;
  }
  return position;
}

bool Intersects(TextRange left, TextRange right) {
  left = left.Normalized();
  right = right.Normalized();
  return left.anchor < right.focus && right.anchor < left.focus;
}

}  // namespace

TextLayoutResult DeterministicTextLayout::Layout(const TextDocument& document,
                                                 float width,
                                                 TextRange selection) {
  if (!std::isfinite(width) || width <= 0.0F ||
      !document.IsValidPosition(selection.anchor) ||
      !document.IsValidPosition(selection.focus)) {
    throw std::invalid_argument("layout width and selection must be valid");
  }
  constexpr float kFontSize = 16.0F;
  constexpr float kLineHeight = 20.0F;
  TextLayoutResult result;
  result.width = width;
  float x = 0.0F;
  float y = 0.0F;
  LogicalPosition position{};
  LogicalPosition line_start{};
  const std::u16string text = document.PlainText();
  for (char16_t unit : text) {
    const float advance = UnitAdvance(unit) * kFontSize;
    if (unit != u'\n' && x > 0.0F && x + advance > width) {
      result.lines.push_back({line_start, position, y + 15.0F, x, kLineHeight});
      y += kLineHeight;
      x = 0.0F;
      line_start = position;
    }
    const LogicalPosition next = Advance(position, unit);
    if (unit == u'\n') {
      result.lines.push_back({line_start, position, y + 15.0F, x, kLineHeight});
      y += kLineHeight;
      x = 0.0F;
      position = next;
      line_start = position;
      continue;
    }
    ClusterGeometry cluster{{position, next}, {x, y, advance, kLineHeight}};
    if (Intersects(cluster.range, selection)) {
      result.selection_rects.push_back(cluster.bounds);
    }
    result.clusters.push_back(cluster);
    x += advance;
    position = next;
  }
  result.lines.push_back({line_start, position, y + 15.0F, x, kLineHeight});
  result.height = y + kLineHeight;
  result.diagnostics.push_back(
      "deterministic-probe-layout: not a canonical shaping backend");
  return result;
}

}  // namespace canvas::poc04
