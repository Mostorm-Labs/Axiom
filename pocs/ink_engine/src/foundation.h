#ifndef CANVAS_POC02_FOUNDATION_H_
#define CANVAS_POC02_FOUNDATION_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "canvas_poc02/ink_engine.h"

namespace canvas::poc02::internal {

class CanonicalEncoder {
 public:
  void U8(uint8_t value);
  void U32(uint32_t value);
  void U64(uint64_t value);
  void F32(float value);
  void String(std::string_view value);
  [[nodiscard]] std::span<const uint8_t> bytes() const { return bytes_; }

 private:
  std::vector<uint8_t> bytes_;
};

float CanonicalFloat(double value);
bool IsFinite(float value);
bool IsValidTransform(const AffineTransform& transform);
Status TransformPoint(const AffineTransform& transform, Vec2 input, Vec2* output);
std::string HashHex(std::span<const uint8_t> bytes);
void EncodeBrush(const BrushDescriptor& brush, CanonicalEncoder* encoder);
void EncodeStroke(const Stroke& stroke, CanonicalEncoder* encoder);
Status ValidateBrush(const BrushDescriptor& brush);
Status ValidateStroke(const Stroke& stroke);

class Pcg32 {
 public:
  Pcg32(uint64_t state, uint64_t sequence);
  uint32_t Next();
  float UnitFloat();
  float SignedUnitFloat();

 private:
  uint64_t state_ = 0;
  uint64_t increment_ = 0;
};

Pcg32 MakeStrokeRandom(const BrushDescriptor& brush, StrokeId stroke_id,
                       uint64_t stream, uint64_t item_index);

}  // namespace canvas::poc02::internal

#endif  // CANVAS_POC02_FOUNDATION_H_
