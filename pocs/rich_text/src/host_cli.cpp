#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "canvas_poc04/rich_text.h"

int main(int argc, char** argv) {
  using namespace canvas::poc04;
  uint32_t lifecycle = 100;
  if (argc == 2 && std::string_view(argv[1]).starts_with("--lifecycle=")) {
    lifecycle = static_cast<uint32_t>(
        std::stoul(std::string(argv[1]).substr(std::string("--lifecycle=").size())));
  }
  std::string digest;
  double max_input_ms = 0.0;
  for (uint32_t index = 0; index < lifecycle; ++index) {
    auto document = std::make_shared<TextDocument>();
    TextEditSession session(document);
    session.Focus();
    const auto started = std::chrono::steady_clock::now();
    session.InsertText(u"Canvas v2\nEnglish ");
    session.BeginComposition();
    session.UpdateComposition(u"中文拼音", 4, 4);
    session.CommitComposition();
    const auto ended = std::chrono::steady_clock::now();
    max_input_ms = std::max(
        max_input_ms,
        std::chrono::duration<double, std::milli>(ended - started).count());
    session.Blur();
    digest = document->Digest();
  }
  std::cout << "{\"backend\":\"host-core\",\"lifecycle\":" << lifecycle
            << ",\"digest\":\"" << digest << "\",\"max_input_ms\":"
            << max_input_ms << "}\n";
  return 0;
}
