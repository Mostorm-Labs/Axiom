#include "foundation.h"

#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "xxhash.h"

namespace canvas::poc02 {

std::string_view StatusName(Status status) {
  switch (status) {
    case Status::kOk: return "Ok";
    case Status::kInvalidArgument: return "InvalidArgument";
    case Status::kInvalidState: return "InvalidState";
    case Status::kUnsupportedVersion: return "UnsupportedVersion";
    case Status::kSequenceError: return "SequenceError";
    case Status::kInputOverrun: return "InputOverrun";
    case Status::kNotFound: return "NotFound";
    case Status::kStaleGeneration: return "StaleGeneration";
    case Status::kParseError: return "ParseError";
  }
  return "Unknown";
}

std::string StrokeDigest(const Stroke& stroke) {
  internal::CanonicalEncoder encoder;
  internal::EncodeStroke(stroke, &encoder);
  return internal::HashHex(encoder.bytes());
}

std::string NumericConformanceDigest() {
  internal::CanonicalEncoder encoder;
  encoder.String("canvas-poc02-numeric-conformance-v1");
  const std::vector<double> accepted{
      0.0,
      -0.0,
      static_cast<double>(std::numeric_limits<float>::denorm_min()),
      1.000000059604644775390625,
      std::nextafter(1.000000059604644775390625, 2.0),
      16777216.0,
  };
  for (double value : accepted) {
    encoder.U8(1);
    encoder.F32(internal::CanonicalFloat(value));
  }
  const std::vector<double> rejected{
      16777217.0,
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN(),
  };
  for (double value : rejected) {
    try {
      encoder.F32(internal::CanonicalFloat(value));
      encoder.U8(1);
    } catch (const std::invalid_argument&) {
      encoder.U8(0);
    }
  }
  return internal::HashHex(encoder.bytes());
}

}  // namespace canvas::poc02

namespace canvas::poc02::internal {
namespace {

constexpr double kMaximumCanonicalCoordinate = 16777216.0;

}  // namespace

void CanonicalEncoder::U8(uint8_t value) { bytes_.push_back(value); }

void CanonicalEncoder::U32(uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes_.push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
  }
}

void CanonicalEncoder::U64(uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes_.push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
  }
}

void CanonicalEncoder::F32(float value) {
  U32(std::bit_cast<uint32_t>(CanonicalFloat(value)));
}

void CanonicalEncoder::String(std::string_view value) {
  U64(static_cast<uint64_t>(value.size()));
  bytes_.insert(bytes_.end(), value.begin(), value.end());
}

float CanonicalFloat(double value) {
  if (!std::isfinite(value) || std::abs(value) > kMaximumCanonicalCoordinate) {
    throw std::invalid_argument("non-finite or out-of-range canonical float");
  }
  const float converted = static_cast<float>(value);
  if (!std::isfinite(converted)) {
    throw std::invalid_argument("canonical float conversion overflow");
  }
  return converted == 0.0F ? 0.0F : converted;
}

bool IsFinite(float value) { return std::isfinite(value); }

bool IsValidTransform(const AffineTransform& transform) {
  const float values[] = {transform.m00, transform.m01, transform.m10,
                          transform.m11, transform.tx, transform.ty};
  for (float value : values) {
    if (!IsFinite(value)) return false;
  }
  const double determinant = static_cast<double>(transform.m00) * transform.m11 -
                             static_cast<double>(transform.m01) * transform.m10;
  return std::isfinite(determinant) && std::abs(determinant) >= 1.0e-12;
}

Status TransformPoint(const AffineTransform& transform, Vec2 input, Vec2* output) {
  if (output == nullptr || !IsValidTransform(transform) ||
      !IsFinite(input.x) || !IsFinite(input.y)) {
    return Status::kInvalidArgument;
  }
  try {
    const double x = static_cast<double>(transform.m00) * input.x +
                     static_cast<double>(transform.m01) * input.y + transform.tx;
    const double y = static_cast<double>(transform.m10) * input.x +
                     static_cast<double>(transform.m11) * input.y + transform.ty;
    output->x = CanonicalFloat(x);
    output->y = CanonicalFloat(y);
  } catch (const std::invalid_argument&) {
    return Status::kInvalidArgument;
  }
  return Status::kOk;
}

std::string HashHex(std::span<const uint8_t> bytes) {
  const XXH128_hash_t hash = XXH3_128bits(bytes.data(), bytes.size());
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::nouppercase
         << std::setw(16) << hash.high64 << std::setw(16) << hash.low64;
  return stream.str();
}

void EncodeBrush(const BrushDescriptor& brush, CanonicalEncoder* encoder) {
  encoder->U8(static_cast<uint8_t>(brush.type));
  encoder->U32(brush.brush_version);
  encoder->U32(brush.algorithm_version);
  encoder->F32(brush.size);
  encoder->F32(brush.spacing);
  encoder->F32(brush.opacity);
  encoder->F32(brush.jitter);
  encoder->String(brush.resource_id);
  encoder->String(brush.resource_content_hash);
}

void EncodeStroke(const Stroke& stroke, CanonicalEncoder* encoder) {
  encoder->String("canvas-poc02-stroke-v1");
  encoder->U64(stroke.id);
  EncodeBrush(stroke.brush, encoder);
  encoder->U64(stroke.confirmed_samples.size());
  for (const auto& sample : stroke.confirmed_samples) {
    encoder->F32(sample.position.x);
    encoder->F32(sample.position.y);
    encoder->F32(sample.pressure);
    encoder->F32(sample.tilt.x);
    encoder->F32(sample.tilt.y);
    encoder->U64(sample.timestamp_us);
  }
  encoder->U64(stroke.vector_points.size());
  for (const auto& point : stroke.vector_points) {
    encoder->F32(point.position.x);
    encoder->F32(point.position.y);
    encoder->F32(point.radius);
  }
  encoder->U64(stroke.dabs.size());
  for (const auto& dab : stroke.dabs) {
    encoder->F32(dab.position.x);
    encoder->F32(dab.position.y);
    encoder->F32(dab.radius);
    encoder->F32(dab.rotation_degrees);
    encoder->F32(dab.opacity);
  }
}

Status ValidateBrush(const BrushDescriptor& brush) {
  if (brush.brush_version != 1 || brush.algorithm_version != 1) {
    return Status::kUnsupportedVersion;
  }
  if ((brush.type != BrushType::kVector && brush.type != BrushType::kDab) ||
      !IsFinite(brush.size) || !IsFinite(brush.spacing) ||
      !IsFinite(brush.opacity) || !IsFinite(brush.jitter) ||
      brush.size <= 0.0F || brush.size > 4096.0F ||
      brush.spacing <= 0.0F || brush.spacing > 8.0F ||
      brush.opacity < 0.0F || brush.opacity > 1.0F ||
      brush.jitter < 0.0F || brush.jitter > 1.0F) {
    return Status::kInvalidArgument;
  }
  return Status::kOk;
}

Status ValidateStroke(const Stroke& stroke) {
  if (stroke.id == 0 || ValidateBrush(stroke.brush) != Status::kOk ||
      stroke.confirmed_samples.empty()) {
    return Status::kInvalidArgument;
  }
  if (stroke.brush.type == BrushType::kVector &&
      (stroke.vector_points.empty() || !stroke.dabs.empty())) {
    return Status::kInvalidArgument;
  }
  if (stroke.brush.type == BrushType::kDab &&
      (stroke.dabs.empty() || !stroke.vector_points.empty())) {
    return Status::kInvalidArgument;
  }
  uint64_t prior_timestamp = 0;
  bool first = true;
  for (const auto& sample : stroke.confirmed_samples) {
    if (!IsFinite(sample.position.x) || !IsFinite(sample.position.y) ||
        !IsFinite(sample.pressure) || !IsFinite(sample.tilt.x) ||
        !IsFinite(sample.tilt.y) || sample.pressure < 0.0F ||
        sample.pressure > 1.0F || (!first && sample.timestamp_us < prior_timestamp)) {
      return Status::kInvalidArgument;
    }
    first = false;
    prior_timestamp = sample.timestamp_us;
  }
  return Status::kOk;
}

Pcg32::Pcg32(uint64_t state, uint64_t sequence)
    : increment_((sequence << 1U) | 1U) {
  Next();
  state_ += state;
  Next();
}

uint32_t Pcg32::Next() {
  const uint64_t old_state = state_;
  state_ = old_state * 6364136223846793005ULL + increment_;
  const uint32_t xorshifted =
      static_cast<uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
  const uint32_t rotation = static_cast<uint32_t>(old_state >> 59U);
  return (xorshifted >> rotation) |
         (xorshifted << ((0U - rotation) & 31U));
}

float Pcg32::UnitFloat() {
  return static_cast<float>(Next() >> 8U) * (1.0F / 16777216.0F);
}

float Pcg32::SignedUnitFloat() { return UnitFloat() * 2.0F - 1.0F; }

Pcg32 MakeStrokeRandom(const BrushDescriptor& brush, StrokeId stroke_id,
                       uint64_t stream, uint64_t item_index) {
  CanonicalEncoder encoder;
  encoder.String("canvas-poc02-pcg32-v1");
  encoder.U32(brush.algorithm_version);
  encoder.U32(brush.brush_version);
  encoder.U64(stroke_id);
  encoder.U64(stream);
  encoder.U64(item_index);
  const XXH128_hash_t hash = XXH3_128bits(encoder.bytes().data(), encoder.bytes().size());
  return Pcg32(hash.low64, hash.high64);
}

}  // namespace canvas::poc02::internal
