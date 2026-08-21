#ifndef CANVAS_POC04_CANONICAL_BEHAVIOR_FIXTURE_H_
#define CANVAS_POC04_CANONICAL_BEHAVIOR_FIXTURE_H_

#include <string>
#include <string_view>

namespace canvas::poc04 {

struct CanonicalBehaviorArtifact {
  std::string json;
  bool passed = false;
};

CanonicalBehaviorArtifact BuildCanonicalBehaviorArtifact(
    std::string_view platform, std::string latin_font_path,
    std::string cjk_font_path);

}  // namespace canvas::poc04

#endif  // CANVAS_POC04_CANONICAL_BEHAVIOR_FIXTURE_H_
