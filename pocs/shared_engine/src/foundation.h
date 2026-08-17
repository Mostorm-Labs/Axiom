#ifndef CANVAS_POC_FOUNDATION_H_
#define CANVAS_POC_FOUNDATION_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "canvas_poc/canvas_poc.h"

namespace canvas::poc01 {

struct Hash128 {
  uint64_t low = 0;
  uint64_t high = 0;

  bool operator==(const Hash128&) const = default;
};

Hash128 HashBytes(std::span<const uint8_t> bytes);
std::string HashHex(Hash128 hash);

void SetLastError(std::string message);
void ClearLastError();
std::string_view GetLastError();

bool IsFinite(float value);
float CanonicalizeF32(float value);

class CanonicalEncoder {
 public:
  void U8(uint8_t value);
  void U32(uint32_t value);
  void U64(uint64_t value);
  void F32(float value);
  void Bytes(std::span<const uint8_t> value);
  void String(std::string_view value);

  [[nodiscard]] std::span<const uint8_t> data() const { return bytes_; }

 private:
  std::vector<uint8_t> bytes_;
};

canvas_poc_status_t CopyToCaller(std::string_view value, char* buffer,
                                 size_t buffer_size,
                                 size_t* out_required_size,
                                 bool include_terminator);
canvas_poc_status_t CopyToCaller(std::span<const uint8_t> value,
                                 uint8_t* buffer, size_t buffer_size,
                                 size_t* out_required_size);

}  // namespace canvas::poc01

#endif  // CANVAS_POC_FOUNDATION_H_
