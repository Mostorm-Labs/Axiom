#ifndef CANVAS_POC04_FOUNDATION_H_
#define CANVAS_POC04_FOUNDATION_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "canvas_poc04/canvas_poc04.h"

namespace canvas::poc04 {

void SetLastError(std::string message);
void ClearLastError();
std::string_view GetLastError();

class CanonicalEncoder {
 public:
  void U8(uint8_t value);
  void U16(uint16_t value);
  void U32(uint32_t value);
  void U64(uint64_t value);
  void F32(float value);
  void String(std::string_view value);
  void String16(std::u16string_view value);
  [[nodiscard]] std::span<const uint8_t> data() const { return bytes_; }

 private:
  std::vector<uint8_t> bytes_;
};

std::string Xxh3Hex(std::span<const uint8_t> bytes);
canvas_poc04_status_t CopyString(std::string_view value, char* buffer,
                                 size_t buffer_size, size_t* required_size);

}  // namespace canvas::poc04

#endif  // CANVAS_POC04_FOUNDATION_H_
