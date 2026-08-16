#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "android_gles_adapter.h"
#include "canvas_poc/canvas_poc.h"
#include "foundation.h"
#include "platform_bridge_internal.h"

namespace {

canvas_poc_handle_t g_runtime = 0;
canvas_poc_handle_t g_document = 0;
std::unique_ptr<canvas::poc01::AndroidGlesAdapter> g_surface;
std::string g_digest;

std::vector<uint8_t> Bytes(JNIEnv* env, jbyteArray source) {
  const jsize size = env->GetArrayLength(source);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  env->GetByteArrayRegion(source, 0, size,
                          reinterpret_cast<jbyte*>(bytes.data()));
  return bytes;
}

std::string LastError() {
  size_t required = 0;
  canvas_poc_last_error(nullptr, 0, &required);
  std::vector<char> value(required);
  canvas_poc_last_error(value.data(), value.size(), &required);
  return value.empty() ? "unknown" : value.data();
}

void Reset() {
  g_surface.reset();
  if (g_document != 0) {
    canvas_poc_document_destroy(g_document);
    g_document = 0;
  }
  if (g_runtime != 0) {
    canvas_poc_runtime_destroy(g_runtime);
    g_runtime = 0;
  }
  g_digest.clear();
}

jstring Failure(JNIEnv* env, const char* action, canvas_poc_status_t status) {
  const std::string message = std::string("FAIL ") + action + ": " +
                              canvas_poc_status_message(status) + ": " +
                              LastError();
  __android_log_print(ANDROID_LOG_ERROR, "CanvasPOC01", "%s",
                      message.c_str());
  return env->NewStringUTF(message.c_str());
}

canvas_poc_status_t LoadState(const std::vector<uint8_t>& checker,
                              const std::vector<uint8_t>& font,
                              const std::vector<uint8_t>& replay) {
  Reset();
  canvas_poc_runtime_config_v1 runtime_config{};
  runtime_config.struct_size = sizeof(runtime_config);
  runtime_config.abi_version = CANVAS_POC_ABI_VERSION;
  canvas_poc_status_t status =
      canvas_poc_runtime_create(&runtime_config, &g_runtime);
  if (status != CANVAS_POC_STATUS_OK) return status;
  status = canvas_poc_runtime_register_asset(
      g_runtime, "checker.png", 11, checker.data(), checker.size());
  if (status != CANVAS_POC_STATUS_OK) return status;
  status = canvas_poc_runtime_register_asset(
      g_runtime, "roboto.ttf", 10, font.data(), font.size());
  if (status != CANVAS_POC_STATUS_OK) return status;

  canvas_poc_document_config_v1 config{};
  config.struct_size = sizeof(config);
  config.abi_version = CANVAS_POC_ABI_VERSION;
  config.page_width = 800;
  config.page_height = 600;
  config.background_rgba[0] = 244;
  config.background_rgba[1] = 245;
  config.background_rgba[2] = 247;
  config.background_rgba[3] = 255;
  status = canvas_poc_document_create(g_runtime, &config, &g_document);
  if (status != CANVAS_POC_STATUS_OK) return status;
  status = canvas_poc_document_apply_ndjson(
      g_document, reinterpret_cast<const char*>(replay.data()), replay.size());
  if (status != CANVAS_POC_STATUS_OK) return status;
  char digest[33]{};
  size_t digest_size = 0;
  status = canvas_poc_document_digest(g_document, digest, sizeof(digest),
                                      &digest_size);
  if (status == CANVAS_POC_STATUS_OK) g_digest = digest;
  return status;
}

canvas_poc_status_t AddSmokeNodes() {
  std::ostringstream generated;
  uint64_t sequence = 8;
  for (uint64_t id = 1000; id < 1996; ++id, ++sequence) {
    const uint64_t index = id - 1000;
    generated << "{\"v\":1,\"seq\":" << sequence
              << ",\"op\":\"create\",\"node\":{\"id\":" << id
              << ",\"type\":\"rect\",\"order\":" << (100 + index)
              << ",\"x\":" << (index % 40U) * 20U
              << ",\"y\":" << (index / 40U) * 20U
              << ",\"width\":12,\"height\":12,"
                 "\"color\":[64,120,220,96]}}\n";
  }
  const std::string operations = generated.str();
  return canvas_poc_document_apply_ndjson(
      g_document, operations.data(), operations.size());
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_CanvasPocView_nativeLoad(
    JNIEnv* env, jobject, jbyteArray checker_array, jbyteArray font_array,
    jbyteArray replay_array) {
  const std::vector<uint8_t> checker = Bytes(env, checker_array);
  const std::vector<uint8_t> font = Bytes(env, font_array);
  const std::vector<uint8_t> replay = Bytes(env, replay_array);
  const canvas_poc_status_t status = LoadState(checker, font, replay);
  if (status != CANVAS_POC_STATUS_OK) return Failure(env, "load", status);
  const std::string result = std::string("READY ") + g_digest;
  return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_CanvasPocView_nativeAttach(
    JNIEnv* env, jobject, jobject surface, jint width, jint height) {
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) {
    return env->NewStringUTF("FAIL native window");
  }
  g_surface = std::make_unique<canvas::poc01::AndroidGlesAdapter>();
  const canvas_poc_status_t status = g_surface->Attach(
      window, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  ANativeWindow_release(window);
  if (status != CANVAS_POC_STATUS_OK) return Failure(env, "attach", status);
  return env->NewStringUTF("ATTACHED ganesh-gles3");
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_CanvasPocView_nativeRender(JNIEnv* env, jobject,
                                                    jstring output_path) {
  if (g_surface == nullptr) return env->NewStringUTF("FAIL no surface");
  std::shared_ptr<canvas::poc01::Document> document =
      canvas::poc01::ResolveDocumentForPlatform(g_document);
  if (document == nullptr) return env->NewStringUTF("FAIL no document");
  std::vector<uint8_t> rgba;
  const canvas_poc_status_t status = g_surface->Render(*document, &rgba);
  if (status != CANVAS_POC_STATUS_OK) return Failure(env, "render", status);
  const char* path = env->GetStringUTFChars(output_path, nullptr);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  env->ReleaseStringUTFChars(output_path, path);
  if (!output) return env->NewStringUTF("FAIL RGBA artifact");
  output.write(reinterpret_cast<const char*>(rgba.data()),
               static_cast<std::streamsize>(rgba.size()));
  const std::string hash =
      canvas::poc01::HashHex(canvas::poc01::HashBytes(rgba));
  const std::string result =
      "{\"platform\":\"android\",\"backend\":\"ganesh-gles3\","
      "\"digest\":\"" + g_digest + "\","
      "\"pixel_hash\":\"" +
      hash + "\"}";
  __android_log_print(ANDROID_LOG_INFO, "CanvasPOC01",
                      "CANVAS_POC01_RESULT %s", result.c_str());
  return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_CanvasPocView_nativeRunAcceptance(
    JNIEnv* env, jobject, jobject surface, jint width, jint height,
    jbyteArray checker_array, jbyteArray font_array, jbyteArray replay_array,
    jstring output_path, jint lifecycle_iterations, jint smoke_seconds) {
  if (width != 800 || height != 600 || lifecycle_iterations <= 0 ||
      smoke_seconds < 0) {
    canvas::poc01::SetLastError(
        "Android acceptance requires 800x600 and valid iteration counts");
    return Failure(env, "acceptance arguments",
                   CANVAS_POC_STATUS_INVALID_ARGUMENT);
  }
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) {
    canvas::poc01::SetLastError("Android native window is unavailable");
    return Failure(env, "native window", CANVAS_POC_STATUS_PLATFORM_ERROR);
  }
  struct ReleaseWindow {
    ANativeWindow* window;
    ~ReleaseWindow() { ANativeWindow_release(window); }
  } release_window{window};

  const std::vector<uint8_t> checker = Bytes(env, checker_array);
  const std::vector<uint8_t> font = Bytes(env, font_array);
  const std::vector<uint8_t> replay = Bytes(env, replay_array);
  const char* raw_path = env->GetStringUTFChars(output_path, nullptr);
  const std::string rgba_path(raw_path);
  env->ReleaseStringUTFChars(output_path, raw_path);

  std::string expected_digest;
  std::vector<uint8_t> pixels;
  for (int iteration = 0; iteration < lifecycle_iterations; ++iteration) {
    canvas_poc_status_t status = LoadState(checker, font, replay);
    if (status != CANVAS_POC_STATUS_OK) return Failure(env, "load", status);
    if (iteration == 0) {
      expected_digest = g_digest;
    } else if (g_digest != expected_digest) {
      canvas::poc01::SetLastError(
          "Android digest changed across lifecycle runs");
      return Failure(env, "lifecycle digest", CANVAS_POC_STATUS_PARSE_ERROR);
    }
    canvas::poc01::AndroidGlesAdapter adapter;
    status = adapter.Attach(window, static_cast<uint32_t>(width),
                            static_cast<uint32_t>(height));
    if (status != CANVAS_POC_STATUS_OK) return Failure(env, "attach", status);
    const std::shared_ptr<canvas::poc01::Document> document =
        canvas::poc01::ResolveDocumentForPlatform(g_document);
    status = adapter.Render(*document, &pixels);
    if (status != CANVAS_POC_STATUS_OK) return Failure(env, "render", status);
  }
  if (expected_digest != "47826449b895ac4f4a57b4f386379775") {
    canvas::poc01::SetLastError(
        "Android digest differs from reviewed fixture");
    return Failure(env, "digest", CANVAS_POC_STATUS_PARSE_ERROR);
  }
  std::ofstream output(rgba_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    canvas::poc01::SetLastError("Android RGBA artifact could not be created");
    return Failure(env, "artifact", CANVAS_POC_STATUS_PLATFORM_ERROR);
  }
  output.write(reinterpret_cast<const char*>(pixels.data()),
               static_cast<std::streamsize>(pixels.size()));
  output.close();
  const std::string reference_hash =
      canvas::poc01::HashHex(canvas::poc01::HashBytes(pixels));

  uint64_t smoke_frames = 0;
  double max_frame_ms = 0.0;
  if (smoke_seconds > 0) {
    canvas_poc_status_t status = LoadState(checker, font, replay);
    if (status != CANVAS_POC_STATUS_OK) return Failure(env, "smoke load", status);
    status = AddSmokeNodes();
    if (status != CANVAS_POC_STATUS_OK) {
      return Failure(env, "smoke nodes", status);
    }
    canvas::poc01::AndroidGlesAdapter adapter;
    status = adapter.Attach(window, static_cast<uint32_t>(width),
                            static_cast<uint32_t>(height));
    if (status != CANVAS_POC_STATUS_OK) {
      return Failure(env, "smoke attach", status);
    }
    const std::shared_ptr<canvas::poc01::Document> document =
        canvas::poc01::ResolveDocumentForPlatform(g_document);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(smoke_seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      const auto start = std::chrono::steady_clock::now();
      status = adapter.Render(*document, &pixels);
      if (status != CANVAS_POC_STATUS_OK) {
        return Failure(env, "smoke render", status);
      }
      const double frame_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start)
                                  .count();
      max_frame_ms = std::max(max_frame_ms, frame_ms);
      ++smoke_frames;
    }
    if (max_frame_ms > 100.0) {
      canvas::poc01::SetLastError(
          "Android GLES smoke frame exceeded 100 ms");
      return Failure(env, "smoke budget", CANVAS_POC_STATUS_RENDER_ERROR);
    }
  }

  std::ostringstream result;
  result << "{\"platform\":\"android\",\"backend\":\"ganesh-gles3\","
            "\"digest\":\""
         << expected_digest << "\",\"pixel_hash\":\"" << reference_hash
         << "\",\"lifecycle\":" << lifecycle_iterations
         << ",\"smoke_seconds\":" << smoke_seconds
         << ",\"smoke_frames\":" << smoke_frames
         << ",\"max_frame_ms\":" << max_frame_ms << "}";
  Reset();
  const std::string serialized = result.str();
  __android_log_print(ANDROID_LOG_INFO, "CanvasPOC01",
                      "CANVAS_POC01_RESULT %s", serialized.c_str());
  return env->NewStringUTF(serialized.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_dev_mostorm_canvas_CanvasPocView_nativeDetach(JNIEnv*, jobject) {
  g_surface.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_mostorm_canvas_CanvasPocView_nativeDestroy(JNIEnv*, jobject) {
  Reset();
}
