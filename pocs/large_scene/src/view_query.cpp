#include "canvas/poc03/large_scene.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace canvas::poc03 {
namespace {

uint16_t ScaleBucket(float zoom, float dpr) {
  const float scale = zoom * dpr;
  if (!std::isfinite(scale) || scale <= 0.0F) {
    throw std::invalid_argument("view scale must be finite and positive");
  }
  const float logarithmic = std::log2(scale) * 8.0F + 128.0F;
  return static_cast<uint16_t>(std::clamp(std::lround(logarithmic), 0L,
                                          255L));
}

Bounds WorldToScreen(const Bounds& world, const ViewState& view) {
  const float scale = view.zoom * view.dpr;
  return Bounds{(world.left - view.world_viewport.left) * scale,
                (world.top - view.world_viewport.top) * scale,
                (world.right - view.world_viewport.left) * scale,
                (world.bottom - view.world_viewport.top) * scale};
}

}  // namespace

ViewQueryResult QueryView(const RuntimeScene& scene, const ViewState& view,
                          const std::optional<Bounds>& world_damage) {
  if (view.view_id == 0 || !view.world_viewport.IsFiniteAndOrdered() ||
      view.world_viewport.left == view.world_viewport.right ||
      view.world_viewport.top == view.world_viewport.bottom ||
      view.pixel_width == 0 || view.pixel_height == 0 ||
      !std::isfinite(view.zoom) || view.zoom <= 0.0F ||
      !std::isfinite(view.dpr) || view.dpr <= 0.0F) {
    throw std::invalid_argument("invalid ViewState");
  }
  ViewQueryResult result;
  if (world_damage && !world_damage->IsFiniteAndOrdered()) {
    throw std::invalid_argument("invalid world-space damage");
  }
  result.scale_bucket = ScaleBucket(view.zoom, view.dpr);
  result.candidates = scene.spatial_index().Query(view.world_viewport);
  result.visible.reserve(result.candidates.size());
  for (const uint32_t slot : result.candidates) {
    const auto record = scene.RecordAt(slot);
    if (record && record->bounds.Intersects(view.world_viewport)) {
      result.visible.push_back(slot);
    }
  }
  std::sort(result.visible.begin(), result.visible.end(),
            [&](uint32_t left, uint32_t right) {
              const auto left_record = scene.RecordAt(left);
              const auto right_record = scene.RecordAt(right);
              return left_record->order < right_record->order ||
                     (left_record->order == right_record->order &&
                      left_record->id < right_record->id);
            });
  const Bounds damage = world_damage
                            ? Bounds{std::max(world_damage->left,
                                              view.world_viewport.left),
                                     std::max(world_damage->top,
                                              view.world_viewport.top),
                                     std::min(world_damage->right,
                                              view.world_viewport.right),
                                     std::min(world_damage->bottom,
                                              view.world_viewport.bottom)}
                            : view.world_viewport;
  result.screen_damage = damage.left < damage.right && damage.top < damage.bottom
                             ? WorldToScreen(damage, view)
                             : Bounds{};
  return result;
}

std::vector<uint64_t> HitTest(const RuntimeScene& scene, const ViewState& view,
                              float world_x, float world_y,
                              float tolerance) {
  if (!std::isfinite(world_x) || !std::isfinite(world_y) ||
      !std::isfinite(tolerance) || tolerance < 0.0F) {
    throw std::invalid_argument("invalid hit-test input");
  }
  const Bounds probe{world_x - tolerance, world_y - tolerance,
                     world_x + tolerance + 0.0001F,
                     world_y + tolerance + 0.0001F};
  std::vector<uint32_t> slots = scene.spatial_index().Query(probe);
  std::vector<NodeRecord> records;
  for (const uint32_t slot : slots) {
    const auto record = scene.RecordAt(slot);
    if (record && record->bounds.Expanded(tolerance).Intersects(probe) &&
        record->bounds.Intersects(view.world_viewport)) {
      records.push_back(*record);
    }
  }
  std::sort(records.begin(), records.end(),
            [](const NodeRecord& left, const NodeRecord& right) {
              return left.order > right.order ||
                     (left.order == right.order && left.id > right.id);
            });
  std::vector<uint64_t> ids;
  ids.reserve(records.size());
  for (const NodeRecord& record : records) {
    ids.push_back(record.id);
  }
  return ids;
}

std::optional<uint64_t> SelectFirstUnlocked(const RuntimeScene& scene,
                                            std::span<const uint64_t> hits) {
  for (const uint64_t id : hits) {
    const auto slot = scene.SlotFor(id);
    if (slot) {
      const auto record = scene.RecordAt(*slot);
      if (record && !record->locked) {
        return id;
      }
    }
  }
  return std::nullopt;
}

std::optional<float> SnapNearestX(const RuntimeScene& scene,
                                  std::span<const uint32_t> candidates,
                                  float world_x, float tolerance) {
  std::optional<float> best;
  float best_distance = tolerance;
  for (const uint32_t slot : candidates) {
    const auto record = scene.RecordAt(slot);
    if (!record) {
      continue;
    }
    for (const float edge : {record->bounds.left, record->bounds.right}) {
      const float distance = std::abs(edge - world_x);
      if (distance < best_distance ||
          (distance == best_distance && (!best || edge < *best))) {
        best = edge;
        best_distance = distance;
      }
    }
  }
  return best;
}

}  // namespace canvas::poc03
