#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "canvas_poc/canvas_poc.h"

namespace {

namespace fs = std::filesystem;

std::vector<uint8_t> ReadBytes(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open " + path.string());
  }
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

std::string ReadText(const fs::path& path) {
  const std::vector<uint8_t> bytes = ReadBytes(path);
  return std::string(bytes.begin(), bytes.end());
}

std::string LastError() {
  size_t required = 0;
  canvas_poc_last_error(nullptr, 0, &required);
  std::vector<char> buffer(required);
  if (canvas_poc_last_error(buffer.data(), buffer.size(), &required) !=
      CANVAS_POC_STATUS_OK) {
    return "unable to read detailed error";
  }
  return buffer.empty() ? std::string() : std::string(buffer.data());
}

void Check(canvas_poc_status_t status, std::string_view action) {
  if (status != CANVAS_POC_STATUS_OK) {
    throw std::runtime_error(std::string(action) + ": " +
                             canvas_poc_status_message(status) + ": " +
                             LastError());
  }
}

struct Options {
  fs::path fixture = fs::path(CANVAS_POC01_SOURCE_DIR) / "fixtures/scene.ndjson";
  fs::path checker = fs::path(CANVAS_POC01_SOURCE_DIR) / "fixtures/checker.png";
  fs::path font = fs::path(CANVAS_POC01_SOURCE_DIR).parent_path().parent_path() /
                  ".deps/assets/Roboto-Regular.ttf";
  fs::path output;
  int lifecycle = 1;
  int smoke_seconds = 0;
  bool update_golden = false;
};

int PositiveInteger(std::string_view value, std::string_view option) {
  const int result = std::stoi(std::string(value));
  if (result <= 0) {
    throw std::runtime_error(std::string(option) + " must be positive");
  }
  return result;
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument.starts_with("--fixture=")) {
      options.fixture = argument.substr(10);
    } else if (argument.starts_with("--checker=")) {
      options.checker = argument.substr(10);
    } else if (argument.starts_with("--font=")) {
      options.font = argument.substr(7);
    } else if (argument.starts_with("--output=")) {
      options.output = argument.substr(9);
    } else if (argument.starts_with("--lifecycle=")) {
      options.lifecycle = PositiveInteger(argument.substr(12), "--lifecycle");
    } else if (argument.starts_with("--smoke=")) {
      options.smoke_seconds = PositiveInteger(argument.substr(8), "--smoke");
    } else if (argument == "--update-golden") {
      options.update_golden = true;
    } else if (argument == "--help") {
      std::cout
          << "canvas_poc01_cli [--fixture=PATH] [--checker=PATH] [--font=PATH] "
             "[--lifecycle=N] [--smoke=SECONDS] [--output=PATH] "
             "[--update-golden]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(argument));
    }
  }
  if (options.update_golden && options.output.empty()) {
    throw std::runtime_error("--update-golden requires --output");
  }
  return options;
}

struct Session {
  canvas_poc_handle_t runtime = 0;
  canvas_poc_handle_t document = 0;
  canvas_poc_handle_t view = 0;

  Session() = default;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&& other) noexcept
      : runtime(other.runtime), document(other.document), view(other.view) {
    other.runtime = 0;
    other.document = 0;
    other.view = 0;
  }

  ~Session() {
    if (view != 0) {
      canvas_poc_view_destroy(view);
    }
    if (document != 0) {
      canvas_poc_document_destroy(document);
    }
    if (runtime != 0) {
      canvas_poc_runtime_destroy(runtime);
    }
  }
};

Session CreateSession(const Options& options, bool add_smoke_nodes = false) {
  Session session;
  canvas_poc_runtime_config_v1 runtime_config{};
  runtime_config.struct_size = sizeof(runtime_config);
  runtime_config.abi_version = CANVAS_POC_ABI_VERSION;
  Check(canvas_poc_runtime_create(&runtime_config, &session.runtime),
        "create runtime");

  const std::vector<uint8_t> checker = ReadBytes(options.checker);
  const std::vector<uint8_t> font = ReadBytes(options.font);
  Check(canvas_poc_runtime_register_asset(
            session.runtime, "checker.png", sizeof("checker.png") - 1,
            checker.data(), checker.size()),
        "register checker");
  Check(canvas_poc_runtime_register_asset(
            session.runtime, "roboto.ttf", sizeof("roboto.ttf") - 1,
            font.data(), font.size()),
        "register font");

  canvas_poc_document_config_v1 document_config{};
  document_config.struct_size = sizeof(document_config);
  document_config.abi_version = CANVAS_POC_ABI_VERSION;
  document_config.page_width = 800;
  document_config.page_height = 600;
  document_config.background_rgba[0] = 244;
  document_config.background_rgba[1] = 245;
  document_config.background_rgba[2] = 247;
  document_config.background_rgba[3] = 255;
  Check(canvas_poc_document_create(session.runtime, &document_config,
                                   &session.document),
        "create document");
  const std::string replay = ReadText(options.fixture);
  Check(canvas_poc_document_apply_ndjson(session.document, replay.data(),
                                         replay.size()),
        "apply fixture");
  if (add_smoke_nodes) {
    std::ostringstream generated;
    uint64_t sequence = 8;
    for (uint64_t id = 1000; id < 1996; ++id, ++sequence) {
      const uint64_t index = id - 1000;
      generated << "{\"v\":1,\"seq\":" << sequence
                << ",\"op\":\"create\",\"node\":{\"id\":" << id
                << ",\"type\":\"rect\",\"order\":" << (100 + index)
                << ",\"x\":" << (index % 40U) * 20U
                << ",\"y\":" << (index / 40U) * 20U
                << ",\"width\":12,\"height\":12,\"color\":[64,120,220,96]}}\n";
    }
    const std::string smoke_replay = generated.str();
    Check(canvas_poc_document_apply_ndjson(
              session.document, smoke_replay.data(), smoke_replay.size()),
          "create 1,000-node smoke scene");
  }

  canvas_poc_view_config_v1 view_config{};
  view_config.struct_size = sizeof(view_config);
  view_config.abi_version = CANVAS_POC_ABI_VERSION;
  view_config.width = 800;
  view_config.height = 600;
  view_config.device_pixel_ratio = 1.0F;
  Check(canvas_poc_view_create_offscreen(session.document, &view_config,
                                         &session.view),
        "create offscreen view");
  return session;
}

std::string Digest(canvas_poc_handle_t document) {
  size_t required = 0;
  const canvas_poc_status_t probe =
      canvas_poc_document_digest(document, nullptr, 0, &required);
  if (probe != CANVAS_POC_STATUS_BUFFER_TOO_SMALL) {
    Check(probe, "query digest size");
  }
  std::vector<char> buffer(required);
  Check(canvas_poc_document_digest(document, buffer.data(), buffer.size(),
                                   &required),
        "read digest");
  return std::string(buffer.data());
}

std::vector<uint8_t> Render(canvas_poc_handle_t view) {
  Check(canvas_poc_view_render(view), "render view");
  size_t required = 0;
  const canvas_poc_status_t probe =
      canvas_poc_view_read_rgba(view, nullptr, 0, &required);
  if (probe != CANVAS_POC_STATUS_BUFFER_TOO_SMALL) {
    Check(probe, "query readback size");
  }
  std::vector<uint8_t> pixels(required);
  Check(canvas_poc_view_read_rgba(view, pixels.data(), pixels.size(), &required),
        "read RGBA");
  return pixels;
}

void WriteOutput(const fs::path& path, const std::vector<uint8_t>& pixels,
                 bool update_golden) {
  if (update_golden && path.extension() != ".rgba") {
    throw std::runtime_error("reviewed host golden output must use .rgba");
  }
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("failed to create " + path.string());
  }
  stream.write(reinterpret_cast<const char*>(pixels.data()),
               static_cast<std::streamsize>(pixels.size()));
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    std::string expected_digest;
    for (int iteration = 0; iteration < options.lifecycle; ++iteration) {
      Session session = CreateSession(options);
      const std::string digest = Digest(session.document);
      if (iteration == 0) {
        expected_digest = digest;
      } else if (digest != expected_digest) {
        throw std::runtime_error("digest changed across lifecycle iterations");
      }
      std::vector<uint8_t> pixels = Render(session.view);
      if (iteration == 0 && !options.output.empty()) {
        WriteOutput(options.output, pixels, options.update_golden);
      }
    }

    double maximum_ms = 0.0;
    uint64_t frame_count = 0;
    if (options.smoke_seconds > 0) {
      Session smoke = CreateSession(options, true);
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(options.smoke_seconds);
      while (std::chrono::steady_clock::now() < deadline) {
        const auto start = std::chrono::steady_clock::now();
        static_cast<void>(Render(smoke.view));
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        maximum_ms = std::max(maximum_ms, elapsed);
        ++frame_count;
      }
      if (maximum_ms > 100.0) {
        throw std::runtime_error("smoke frame exceeded 100 ms");
      }
    }

    std::cout << "{\"backend\":\"software-probe\",\"digest\":\""
              << expected_digest << "\",\"lifecycle\":" << options.lifecycle
              << ",\"smoke_seconds\":" << options.smoke_seconds
              << ",\"smoke_frames\":" << frame_count
              << ",\"max_frame_ms\":" << maximum_ms << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "canvas_poc01_cli: " << error.what() << '\n';
    return 1;
  }
}
