#ifndef CANVAS_POC03_INTERNAL_H_
#define CANVAS_POC03_INTERNAL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "canvas/poc03/large_scene.h"

namespace canvas::poc03::detail {

float CanonicalF32(float value);
void EncodeU8(std::vector<uint8_t>* bytes, uint8_t value);
void EncodeU32(std::vector<uint8_t>* bytes, uint32_t value);
void EncodeU64(std::vector<uint8_t>* bytes, uint64_t value);
void EncodeF32(std::vector<uint8_t>* bytes, float value);
void EncodeBounds(std::vector<uint8_t>* bytes, const Bounds& bounds);
void EncodeRecord(std::vector<uint8_t>* bytes, const NodeRecord& node);
bool ValidRecord(const NodeRecord& node, std::string* error);

}  // namespace canvas::poc03::detail

#endif
