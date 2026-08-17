#include "foundation.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "xxhash.h"

namespace canvas::poc01 {
namespace {

thread_local std::string g_last_error;

}  // namespace

Hash128 HashBytes(std::span<const uint8_t> bytes) {
  const XXH128_hash_t hash = XXH3_128bits(bytes.data(), bytes.size());
  return Hash128{hash.low64, hash.high64};
}

std::string HashHex(Hash128 hash) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::nouppercase
         << std::setw(16) << hash.high << std::setw(16) << hash.low;
  return stream.str();
}

void SetLastError(std::string message) { g_last_error = std::move(message); }

void ClearLastError() { g_last_error.clear(); }

std::string_view GetLastError() { return g_last_error; }

bool IsFinite(float value) { return std::isfinite(value); }

float CanonicalizeF32(float value) {
  if (!IsFinite(value)) {
    throw std::invalid_argument("canonical float32 rejects non-finite value");
  }
  return value == 0.0F ? 0.0F : value;
}

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
  U32(std::bit_cast<uint32_t>(CanonicalizeF32(value)));
}

void CanonicalEncoder::Bytes(std::span<const uint8_t> value) {
  U64(static_cast<uint64_t>(value.size()));
  bytes_.insert(bytes_.end(), value.begin(), value.end());
}

void CanonicalEncoder::String(std::string_view value) {
  Bytes(std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(value.data()), value.size()));
}

canvas_poc_status_t CopyToCaller(std::string_view value, char* buffer,
                                 size_t buffer_size,
                                 size_t* out_required_size,
                                 bool include_terminator) {
  if (out_required_size == nullptr) {
    SetLastError("out_required_size must not be null");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  const size_t required = value.size() + (include_terminator ? 1U : 0U);
  *out_required_size = required;
  if (buffer == nullptr || buffer_size < required) {
    return CANVAS_POC_STATUS_BUFFER_TOO_SMALL;
  }
  if (!value.empty()) {
    std::memcpy(buffer, value.data(), value.size());
  }
  if (include_terminator) {
    buffer[value.size()] = '\0';
  }
  return CANVAS_POC_STATUS_OK;
}

canvas_poc_status_t CopyToCaller(std::span<const uint8_t> value,
                                 uint8_t* buffer, size_t buffer_size,
                                 size_t* out_required_size) {
  if (out_required_size == nullptr) {
    SetLastError("out_required_size must not be null");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  *out_required_size = value.size();
  if (buffer == nullptr || buffer_size < value.size()) {
    return CANVAS_POC_STATUS_BUFFER_TOO_SMALL;
  }
  if (!value.empty()) {
    std::memcpy(buffer, value.data(), value.size());
  }
  return CANVAS_POC_STATUS_OK;
}

}  // namespace canvas::poc01
