#ifndef CANVAS_POC_APPLE_RUNNER_SUPPORT_H_
#define CANVAS_POC_APPLE_RUNNER_SUPPORT_H_

#include <filesystem>
#include <functional>
#include <string>

namespace canvas::poc01 {

std::string RunAppleFixture(const std::filesystem::path& fixture_directory,
                            const std::filesystem::path& font_path,
                            const std::filesystem::path& rgba_output = {},
                            std::string platform = "apple");

std::string RunAppleAcceptance(
    const std::filesystem::path& fixture_directory,
    const std::filesystem::path& font_path,
    const std::filesystem::path& rgba_output,
    std::string platform,
    int lifecycle_iterations,
    int smoke_seconds,
    const std::function<void(bool)>& smoke_phase = {});

}  // namespace canvas::poc01

#endif  // CANVAS_POC_APPLE_RUNNER_SUPPORT_H_
