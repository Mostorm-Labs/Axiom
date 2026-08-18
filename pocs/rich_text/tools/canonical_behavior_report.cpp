#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "canonical_behavior_fixture.h"

int main(int argc, char** argv) {
  std::string platform = "unknown";
  std::string output_path;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument.starts_with("--platform=")) platform = argument.substr(11);
    if (argument.starts_with("--output=")) output_path = argument.substr(9);
  }
  const canvas::poc04::CanonicalBehaviorArtifact artifact =
      canvas::poc04::BuildCanonicalBehaviorArtifact(
          platform, CANVAS_POC04_FONT_PATH, CANVAS_POC04_CJK_FONT_PATH);
  if (output_path.empty()) {
    std::cout << artifact.json;
  } else {
    std::ofstream output(output_path);
    output << artifact.json;
    if (!output) return 2;
  }
  return artifact.passed ? 0 : 1;
}
