#include "canvas/poc03/large_scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace canvas::poc03 {
namespace {

int64_t CellKey(int32_t x, int32_t y) {
  return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(x))
                               << 32U) |
                              static_cast<uint32_t>(y));
}

}  // namespace

SpatialIndex::SpatialIndex(float cell_size) : cell_size_(cell_size) {
  if (!std::isfinite(cell_size) || cell_size <= 0.0F) {
    throw std::invalid_argument("spatial cell size must be finite and positive");
  }
}

void SpatialIndex::Clear(size_t slot_capacity) {
  cells_.clear();
  marks_.assign(slot_capacity, 0U);
  query_generation_ = 0;
}

std::vector<int64_t> SpatialIndex::CellsFor(const Bounds& bounds) const {
  if (!bounds.IsFiniteAndOrdered()) {
    throw std::invalid_argument("spatial bounds are invalid");
  }
  const double first_x_double = std::floor(bounds.left / cell_size_);
  const double first_y_double = std::floor(bounds.top / cell_size_);
  const double last_x_double = std::floor(
      std::nextafter(bounds.right, -std::numeric_limits<float>::infinity()) /
      cell_size_);
  const double last_y_double = std::floor(
      std::nextafter(bounds.bottom, -std::numeric_limits<float>::infinity()) /
      cell_size_);
  if (first_x_double < std::numeric_limits<int32_t>::min() ||
      last_x_double > std::numeric_limits<int32_t>::max() ||
      first_y_double < std::numeric_limits<int32_t>::min() ||
      last_y_double > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("spatial cell coordinate overflow");
  }
  const int32_t first_x = static_cast<int32_t>(first_x_double);
  const int32_t last_x = static_cast<int32_t>(last_x_double);
  const int32_t first_y = static_cast<int32_t>(first_y_double);
  const int32_t last_y = static_cast<int32_t>(last_y_double);
  const uint64_t width = static_cast<uint64_t>(
      static_cast<int64_t>(last_x) - static_cast<int64_t>(first_x) + 1);
  const uint64_t height = static_cast<uint64_t>(
      static_cast<int64_t>(last_y) - static_cast<int64_t>(first_y) + 1);
  if (width > 65536U || height > 65536U || width * height > 1048576U) {
    throw std::overflow_error("one record covers too many spatial cells");
  }
  std::vector<int64_t> result;
  result.reserve(static_cast<size_t>(width * height));
  for (int64_t y = first_y; y <= last_y; ++y) {
    for (int64_t x = first_x; x <= last_x; ++x) {
      result.push_back(CellKey(static_cast<int32_t>(x),
                               static_cast<int32_t>(y)));
    }
  }
  return result;
}

void SpatialIndex::Insert(uint32_t slot, const Bounds& bounds) {
  if (slot >= marks_.size()) {
    marks_.resize(static_cast<size_t>(slot) + 1U, 0U);
  }
  for (const int64_t key : CellsFor(bounds)) {
    cells_[key].push_back(slot);
  }
}

void SpatialIndex::Remove(uint32_t slot, const Bounds& bounds) {
  for (const int64_t key : CellsFor(bounds)) {
    const auto found = cells_.find(key);
    if (found == cells_.end()) {
      continue;
    }
    auto& values = found->second;
    values.erase(std::remove(values.begin(), values.end(), slot), values.end());
    if (values.empty()) {
      cells_.erase(found);
    }
  }
}

void SpatialIndex::Update(uint32_t slot, const Bounds& before,
                          const Bounds& after) {
  if (before == after) {
    return;
  }
  Remove(slot, before);
  Insert(slot, after);
}

std::vector<uint32_t> SpatialIndex::Query(const Bounds& bounds) const {
  if (++query_generation_ == 0U) {
    std::fill(marks_.begin(), marks_.end(), 0U);
    query_generation_ = 1U;
  }
  std::vector<uint32_t> result;
  for (const int64_t key : CellsFor(bounds)) {
    const auto found = cells_.find(key);
    if (found == cells_.end()) {
      continue;
    }
    for (const uint32_t slot : found->second) {
      if (slot >= marks_.size()) {
        throw std::logic_error("spatial index contains an invalid slot");
      }
      if (marks_[slot] != query_generation_) {
        marks_[slot] = query_generation_;
        result.push_back(slot);
      }
    }
  }
  return result;
}

size_t SpatialIndex::EstimatedBytes() const {
  size_t bytes = sizeof(*this) + marks_.capacity() * sizeof(uint32_t);
  for (const auto& [key, slots] : cells_) {
    static_cast<void>(key);
    bytes += sizeof(int64_t) + 32U + slots.capacity() * sizeof(uint32_t);
  }
  return bytes;
}

}  // namespace canvas::poc03
