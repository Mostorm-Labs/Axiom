#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace canvas::windows {

class DocumentStore {
 public:
  static bool saveAtomic(const std::filesystem::path& path,
                         const std::vector<std::uint8_t>& bytes,
                         std::string& error);
  static bool load(const std::filesystem::path& path,
                   std::vector<std::uint8_t>& bytes, std::string& error);
};

}  // namespace canvas::windows
