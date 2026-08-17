#import <Foundation/Foundation.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "runner_support.h"

namespace {

int ParsePositive(std::string_view argument, std::string_view prefix) {
  if (!argument.starts_with(prefix)) {
    throw std::runtime_error("invalid macOS runner argument");
  }
  const int value = std::stoi(std::string(argument.substr(prefix.size())));
  if (value <= 0) throw std::runtime_error("runner count must be positive");
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      int lifecycle = 1;
      int smoke_seconds = 0;
      for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument.starts_with("--lifecycle=")) {
          lifecycle = ParsePositive(argument, "--lifecycle=");
        } else if (argument.starts_with("--smoke=")) {
          smoke_seconds = ParsePositive(argument, "--smoke=");
        } else {
          throw std::runtime_error("unknown macOS runner argument");
        }
      }
      std::cout << canvas::poc01::RunAppleAcceptance(
                       CANVAS_POC01_FIXTURE_DIR, CANVAS_POC01_FONT_PATH,
                       "apple-actual.rgba", "macos", lifecycle,
                       smoke_seconds)
                << '\n';
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "canvas_poc01_macos_runner: " << error.what() << '\n';
      return 1;
    }
  }
}
