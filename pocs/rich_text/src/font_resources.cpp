#include "canvas_poc04/rich_text.h"

#include <iomanip>
#include <sstream>
#include <utility>

#include "sha256.h"

namespace canvas::poc04 {

std::string Sha256Hex(std::span<const uint8_t> input) {
  Sha256 hash;
  hash.Update(input);
  const auto digest = hash.Finish();
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::nouppercase;
  for (uint8_t byte : digest) {
    stream << std::setw(2) << static_cast<uint32_t>(byte);
  }
  return stream.str();
}

bool FontResourceResolver::Register(std::string resource_id,
                                    std::string expected_sha256,
                                    std::span<const uint8_t> bytes) {
  if (resource_id.empty() || expected_sha256.size() != 64 || bytes.empty()) {
    last_diagnostic_ = "font registration requires id, SHA-256, and bytes";
    return false;
  }
  const std::string actual = Sha256Hex(bytes);
  if (actual != expected_sha256) {
    last_diagnostic_ = "font hash mismatch for " + resource_id;
    return false;
  }
  declarations_.insert_or_assign(resource_id, expected_sha256);
  Resource resource{std::move(expected_sha256),
                    {bytes.begin(), bytes.end()}};
  resources_.insert_or_assign(std::move(resource_id), std::move(resource));
  ++generation_;
  last_diagnostic_.clear();
  return true;
}

bool FontResourceResolver::Declare(std::string resource_id,
                                   std::string expected_sha256) {
  if (resource_id.empty() || expected_sha256.size() != 64) {
    last_diagnostic_ = "font declaration requires id and SHA-256";
    return false;
  }
  declarations_.insert_or_assign(std::move(resource_id),
                                 std::move(expected_sha256));
  ++generation_;
  return true;
}

bool FontResourceResolver::Remove(std::string_view resource_id) {
  if (resources_.erase(std::string(resource_id)) == 0) {
    return false;
  }
  ++generation_;
  return true;
}

FontResolution FontResourceResolver::Resolve(
    std::string_view requested_id,
    std::span<const std::string> canonical_fallback_chain) const {
  FontResolution result;
  result.requested_id = requested_id;
  result.generation = generation_;
  auto resolve = [this, &result](std::string_view id) {
    const auto iterator = resources_.find(id);
    if (iterator == resources_.end()) {
      return false;
    }
    result.resolved_id = iterator->first;
    result.content_hash = iterator->second.hash;
    result.bytes = iterator->second.bytes;
    return true;
  };
  if (resolve(requested_id)) {
    result.diagnostic = FontDiagnostic::kOk;
    return result;
  }
  if (declarations_.contains(requested_id)) {
    result.diagnostic = FontDiagnostic::kHashMismatch;
    return result;
  }
  for (const std::string& fallback : canonical_fallback_chain) {
    if (resolve(fallback)) {
      result.diagnostic = FontDiagnostic::kFallbackUsed;
      return result;
    }
  }
  result.diagnostic = FontDiagnostic::kMissing;
  return result;
}

}  // namespace canvas::poc04
