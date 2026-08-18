#include "canvas/poc03/large_scene.h"

#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "xxhash.h"

namespace canvas::poc03 {
namespace {

void AppendU64(std::vector<uint8_t>* bytes, uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes->push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
  }
}

}  // namespace

bool Bounds::IsFiniteAndOrdered() const {
  return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
         std::isfinite(bottom) && left <= right && top <= bottom;
}

bool Bounds::Intersects(const Bounds& other) const {
  return left < other.right && right > other.left && top < other.bottom &&
         bottom > other.top;
}

bool Bounds::Contains(const Bounds& other) const {
  return left <= other.left && top <= other.top && right >= other.right &&
         bottom >= other.bottom;
}

Bounds Bounds::Expanded(float amount) const {
  if (!std::isfinite(amount) || amount < 0.0F) {
    throw std::invalid_argument("bounds expansion must be finite and non-negative");
  }
  const Bounds result{left - amount, top - amount, right + amount,
                      bottom + amount};
  if (!result.IsFiniteAndOrdered()) {
    throw std::overflow_error("bounds expansion overflow");
  }
  return result;
}

Bounds Bounds::Union(const Bounds& first, const Bounds& second) {
  return Bounds{std::min(first.left, second.left),
                std::min(first.top, second.top),
                std::max(first.right, second.right),
                std::max(first.bottom, second.bottom)};
}

std::string CanonicalDigest(std::span<const uint8_t> bytes) {
  const XXH128_hash_t hash = XXH3_128bits(bytes.data(), bytes.size());
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::nouppercase
         << std::setw(16) << hash.high64 << std::setw(16) << hash.low64;
  return stream.str();
}

namespace detail {

float CanonicalF32(float value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument("canonical float32 rejects non-finite value");
  }
  return value == 0.0F ? 0.0F : value;
}

void EncodeU8(std::vector<uint8_t>* bytes, uint8_t value) {
  bytes->push_back(value);
}

void EncodeU32(std::vector<uint8_t>* bytes, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes->push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
  }
}

void EncodeU64(std::vector<uint8_t>* bytes, uint64_t value) {
  AppendU64(bytes, value);
}

void EncodeF32(std::vector<uint8_t>* bytes, float value) {
  EncodeU32(bytes, std::bit_cast<uint32_t>(CanonicalF32(value)));
}

void EncodeBounds(std::vector<uint8_t>* bytes, const Bounds& bounds) {
  EncodeF32(bytes, bounds.left);
  EncodeF32(bytes, bounds.top);
  EncodeF32(bytes, bounds.right);
  EncodeF32(bytes, bounds.bottom);
}

void EncodeRecord(std::vector<uint8_t>* bytes, const NodeRecord& node) {
  EncodeU64(bytes, node.id);
  EncodeU32(bytes, node.order);
  EncodeU8(bytes, static_cast<uint8_t>(node.type));
  EncodeBounds(bytes, node.bounds);
  EncodeU32(bytes, node.rgba);
  EncodeU64(bytes, node.resource_key);
  EncodeU64(bytes, node.content_revision);
  EncodeU8(bytes, node.locked ? 1U : 0U);
}

bool ValidRecord(const NodeRecord& node, std::string* error) {
  constexpr float kMaximumWorldCoordinate = 16777216.0F;
  const uint8_t type = static_cast<uint8_t>(node.type);
  if (node.id == 0 || type < static_cast<uint8_t>(NodeType::kShape) ||
      type > static_cast<uint8_t>(NodeType::kStroke) ||
      !node.bounds.IsFiniteAndOrdered() ||
      node.bounds.left == node.bounds.right ||
      node.bounds.top == node.bounds.bottom || node.content_revision == 0 ||
      std::abs(node.bounds.left) > kMaximumWorldCoordinate ||
      std::abs(node.bounds.top) > kMaximumWorldCoordinate ||
      std::abs(node.bounds.right) > kMaximumWorldCoordinate ||
      std::abs(node.bounds.bottom) > kMaximumWorldCoordinate) {
    if (error != nullptr) {
      *error = "node record has invalid identity, type, bounds, or revision";
    }
    return false;
  }
  return true;
}

}  // namespace detail
}  // namespace canvas::poc03
