#ifndef CANVAS_POC04_SHA256_H_
#define CANVAS_POC04_SHA256_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace canvas::poc04 {

class Sha256 {
 public:
  void Update(std::span<const uint8_t> input);
  std::array<uint8_t, 32> Finish();

 private:
  void Transform(const uint8_t* block);
  std::array<uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                 0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                 0x1f83d9abU, 0x5be0cd19U};
  std::array<uint8_t, 64> buffer_{};
  uint64_t total_size_ = 0;
  size_t buffered_ = 0;
};

}  // namespace canvas::poc04

#endif
