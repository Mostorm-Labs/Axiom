#include "sha256.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace canvas::poc04 {
namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

uint32_t ReadBigEndian(const uint8_t* value) {
  return (static_cast<uint32_t>(value[0]) << 24U) |
         (static_cast<uint32_t>(value[1]) << 16U) |
         (static_cast<uint32_t>(value[2]) << 8U) |
         static_cast<uint32_t>(value[3]);
}

}  // namespace

void Sha256::Transform(const uint8_t* block) {
  std::array<uint32_t, 64> schedule{};
  for (size_t index = 0; index < 16; ++index) {
    schedule[index] = ReadBigEndian(block + index * 4);
  }
  for (size_t index = 16; index < 64; ++index) {
    const uint32_t s0 = std::rotr(schedule[index - 15], 7) ^
                        std::rotr(schedule[index - 15], 18) ^
                        (schedule[index - 15] >> 3U);
    const uint32_t s1 = std::rotr(schedule[index - 2], 17) ^
                        std::rotr(schedule[index - 2], 19) ^
                        (schedule[index - 2] >> 10U);
    schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
  }
  auto [a, b, c, d, e, f, g, h] = state_;
  for (size_t index = 0; index < 64; ++index) {
    const uint32_t upper = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const uint32_t choose = (e & f) ^ (~e & g);
    const uint32_t t1 = h + upper + choose + kRoundConstants[index] + schedule[index];
    const uint32_t lower = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = lower + majority;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::Update(std::span<const uint8_t> input) {
  total_size_ += input.size();
  while (!input.empty()) {
    const size_t amount = std::min(input.size(), buffer_.size() - buffered_);
    std::memcpy(buffer_.data() + buffered_, input.data(), amount);
    buffered_ += amount;
    input = input.subspan(amount);
    if (buffered_ == buffer_.size()) {
      Transform(buffer_.data());
      buffered_ = 0;
    }
  }
}

std::array<uint8_t, 32> Sha256::Finish() {
  const uint64_t bit_size = total_size_ * 8U;
  buffer_[buffered_++] = 0x80U;
  if (buffered_ > 56) {
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_),
              buffer_.end(), 0);
    Transform(buffer_.data());
    buffered_ = 0;
  }
  std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_),
            buffer_.begin() + 56, 0);
  for (size_t index = 0; index < 8; ++index) {
    buffer_[63 - index] = static_cast<uint8_t>(bit_size >> (index * 8U));
  }
  Transform(buffer_.data());
  std::array<uint8_t, 32> result{};
  for (size_t index = 0; index < state_.size(); ++index) {
    for (size_t byte = 0; byte < 4; ++byte) {
      result[index * 4 + byte] =
          static_cast<uint8_t>(state_[index] >> ((3 - byte) * 8U));
    }
  }
  return result;
}

}  // namespace canvas::poc04
