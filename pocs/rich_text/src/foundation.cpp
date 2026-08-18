#include "foundation.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <xxhash.h>

namespace canvas::poc04 {
namespace {

thread_local std::string g_last_error;

}  // namespace

void SetLastError(std::string message) { g_last_error = std::move(message); }
void ClearLastError() { g_last_error.clear(); }
std::string_view GetLastError() { return g_last_error; }

void CanonicalEncoder::U8(uint8_t value) { bytes_.push_back(value); }

void CanonicalEncoder::U16(uint16_t value) {
  U8(static_cast<uint8_t>(value & 0xffU));
  U8(static_cast<uint8_t>((value >> 8U) & 0xffU));
}

void CanonicalEncoder::U32(uint32_t value) {
  for (uint32_t shift = 0; shift < 32; shift += 8) {
    U8(static_cast<uint8_t>((value >> shift) & 0xffU));
  }
}

void CanonicalEncoder::U64(uint64_t value) {
  for (uint32_t shift = 0; shift < 64; shift += 8) {
    U8(static_cast<uint8_t>((value >> shift) & 0xffU));
  }
}

void CanonicalEncoder::F32(float value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument("canonical float rejects non-finite input");
  }
  if (value == 0.0F) {
    value = 0.0F;
  }
  U32(std::bit_cast<uint32_t>(value));
}

void CanonicalEncoder::String(std::string_view value) {
  U64(value.size());
  bytes_.insert(bytes_.end(), value.begin(), value.end());
}

void CanonicalEncoder::String16(std::u16string_view value) {
  U64(value.size());
  for (char16_t unit : value) {
    U16(static_cast<uint16_t>(unit));
  }
}

std::string Xxh3Hex(std::span<const uint8_t> bytes) {
  const XXH128_hash_t hash = XXH3_128bits(bytes.data(), bytes.size());
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::nouppercase
         << std::setw(16) << hash.high64 << std::setw(16) << hash.low64;
  return stream.str();
}

canvas_poc04_status_t CopyString(std::string_view value, char* buffer,
                                 size_t buffer_size, size_t* required_size) {
  if (required_size == nullptr) {
    SetLastError("required_size must not be null");
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  *required_size = value.size() + 1;
  if (buffer == nullptr || buffer_size < *required_size) {
    return CANVAS_POC04_STATUS_BUFFER_TOO_SMALL;
  }
  std::memcpy(buffer, value.data(), value.size());
  buffer[value.size()] = '\0';
  return CANVAS_POC04_STATUS_OK;
}

}  // namespace canvas::poc04
