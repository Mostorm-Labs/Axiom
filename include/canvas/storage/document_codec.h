#pragma once

#include "canvas/document/document.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace canvas::storage {

struct DecodeResult {
  std::optional<document::Document> document;
  std::string error;
};

class DocumentCodec {
 public:
  static std::vector<std::uint8_t> encode(const document::Document& document);
  static DecodeResult decode(const std::vector<std::uint8_t>& bytes);
  // Replaces target only after the entire payload and parent graph decode.
  static bool decodeInto(const std::vector<std::uint8_t>& bytes,
                         document::Document& target, std::string& error);
  static std::vector<std::uint8_t> encodeJson(
      const document::Document& document);
  static DecodeResult decodeJson(const std::vector<std::uint8_t>& bytes);
};

}  // namespace canvas::storage
