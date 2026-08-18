#include "canvas/poc03/large_scene.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace canvas::poc03 {
namespace {

size_t Mix(size_t seed, uint64_t value) {
  return seed ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ULL +
                 (seed << 6U) + (seed >> 2U));
}

}  // namespace

size_t TileKeyHash::operator()(const TileKey& key) const noexcept {
  size_t result = 0;
  result = Mix(result, key.view_id);
  result = Mix(result, key.content_revision);
  result = Mix(result, key.device_generation);
  result = Mix(result, key.backend_capability);
  result = Mix(result, key.scale_bucket);
  result = Mix(result, key.color_space);
  result = Mix(result, static_cast<uint32_t>(key.tile_x));
  return Mix(result, static_cast<uint32_t>(key.tile_y));
}

TileCache::TileCache(size_t byte_budget) : byte_budget_(byte_budget) {
  if (byte_budget == 0) {
    throw std::invalid_argument("tile cache budget must be positive");
  }
}

bool TileCache::Find(const TileKey& key) {
  const auto found = entries_.find(key);
  if (found == entries_.end()) {
    ++stats_.misses;
    return false;
  }
  lru_.splice(lru_.begin(), lru_, found->second.lru);
  ++stats_.hits;
  return true;
}

void TileCache::Put(const TileKey& key, size_t bytes) {
  if (bytes == 0 || bytes > byte_budget_ ||
      key.device_generation != device_generation_) {
    return;
  }
  const auto existing = entries_.find(key);
  if (existing != entries_.end()) {
    stats_.bytes -= existing->second.bytes;
    existing->second.bytes = bytes;
    stats_.bytes += bytes;
    lru_.splice(lru_.begin(), lru_, existing->second.lru);
  } else {
    lru_.push_front(key);
    entries_.emplace(key, Entry{bytes, lru_.begin()});
    stats_.bytes += bytes;
  }
  while (stats_.bytes > byte_budget_ && !lru_.empty()) {
    const TileKey victim = lru_.back();
    const auto found = entries_.find(victim);
    stats_.bytes -= found->second.bytes;
    entries_.erase(found);
    lru_.pop_back();
    ++stats_.evictions;
  }
}

void TileCache::InvalidateWorld(uint64_t view_id, const Bounds& world_dirty,
                                float world_tile_size) {
  if (!world_dirty.IsFiniteAndOrdered() || !std::isfinite(world_tile_size) ||
      world_tile_size <= 0.0F) {
    throw std::invalid_argument("invalid tile invalidation request");
  }
  const int32_t first_x = static_cast<int32_t>(
      std::floor(world_dirty.left / world_tile_size));
  const int32_t first_y = static_cast<int32_t>(
      std::floor(world_dirty.top / world_tile_size));
  const int32_t last_x = static_cast<int32_t>(
      std::floor(world_dirty.right / world_tile_size));
  const int32_t last_y = static_cast<int32_t>(
      std::floor(world_dirty.bottom / world_tile_size));
  for (auto iterator = entries_.begin(); iterator != entries_.end();) {
    const TileKey& key = iterator->first;
    if (key.view_id == view_id && key.tile_x >= first_x &&
        key.tile_x <= last_x && key.tile_y >= first_y && key.tile_y <= last_y) {
      stats_.bytes -= iterator->second.bytes;
      lru_.erase(iterator->second.lru);
      iterator = entries_.erase(iterator);
      ++stats_.invalidations;
    } else {
      ++iterator;
    }
  }
}

void TileCache::Clear() {
  stats_.invalidations += entries_.size();
  stats_.bytes = 0;
  entries_.clear();
  lru_.clear();
}

void TileCache::DeviceLost(uint64_t new_generation) {
  Clear();
  device_generation_ = new_generation;
}

}  // namespace canvas::poc03
