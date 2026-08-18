#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "android_gles_adapter.h"

namespace {

using namespace canvas::poc03;

std::mutex g_mutex;
std::unique_ptr<Document> g_document;
std::unique_ptr<RuntimeScene> g_scene;
std::unique_ptr<AndroidGlesAdapter> g_adapter;
uint32_t g_width = 0;
uint32_t g_height = 0;
float g_pan_x = 0.0F;
float g_pan_y = 0.0F;
float g_zoom = 1.0F;
float g_dpr = 1.0F;
uint64_t g_view_revision = 1U;
uint64_t g_input_events = 0U;

double Percentile(std::vector<double> values, double percentile) {
  std::sort(values.begin(), values.end());
  if (values.empty()) return 0.0;
  const size_t index = static_cast<size_t>(
      std::ceil(percentile * static_cast<double>(values.size())) - 1.0);
  return values[std::min(index, values.size() - 1U)];
}

double ProcessResidentMib() {
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == "VmHWM:") {
      uint64_t kib = 0;
      status >> kib;
      return static_cast<double>(kib) / 1024.0;
    }
    std::string ignored;
    std::getline(status, ignored);
  }
  return 0.0;
}

ViewState CurrentView() {
  const float world_width = static_cast<float>(g_width) / (g_zoom * g_dpr);
  const float world_height = static_cast<float>(g_height) / (g_zoom * g_dpr);
  return ViewState{1U, g_view_revision, 1U,
      Bounds{g_pan_x, g_pan_y, g_pan_x + world_width,
             g_pan_y + world_height},
      g_zoom, g_dpr, g_width, g_height};
}

bool EnsureScene(std::string* error) {
  try {
    if (!g_document) {
      g_document = std::make_unique<Document>(GenerateDocument(
          GeneratorConfig{100000U, 0x43414e5641533033ULL, 1000U, 32.0F}));
      g_scene = std::make_unique<RuntimeScene>(
          SceneCompiler().CompileFull(*g_document));
    }
    return true;
  } catch (const std::exception& exception) {
    *error = exception.what();
    return false;
  }
}

jstring Failure(JNIEnv* env, const std::string& message) {
  const std::string result = "FAIL " + message;
  __android_log_print(ANDROID_LOG_ERROR, "CanvasPOC03", "%s", result.c_str());
  return env->NewStringUTF(result.c_str());
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeAttach(
    JNIEnv* env, jobject, jobject surface, jint width, jint height,
    jfloat density) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (width <= 0 || height <= 0 || !std::isfinite(density) || density <= 0.0F) {
    return Failure(env, "invalid dimensions or density");
  }
  std::string error;
  if (!EnsureScene(&error)) return Failure(env, error);
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) return Failure(env, "native window unavailable");
  g_adapter = std::make_unique<AndroidGlesAdapter>();
  const bool attached = g_adapter->Attach(
      window, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
      &error);
  ANativeWindow_release(window);
  if (!attached) return Failure(env, error);
  g_width = static_cast<uint32_t>(width);
  g_height = static_cast<uint32_t>(height);
  g_dpr = density;
  return env->NewStringUTF("ATTACHED ganesh-gles3 100k");
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeRunAcceptance(
    JNIEnv* env, jobject, jstring output_path, jfloat refresh_rate,
    jint frame_count) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_adapter || !g_scene || frame_count <= 0) {
    return Failure(env, "surface/scene/frame count is invalid");
  }
  SceneCompiler compiler;
  std::string error;
  for (uint32_t update = 0; update < 1000U; ++update) {
    const uint64_t id = 1U + (static_cast<uint64_t>(update) * 7919U) % 100000U;
    NodeRecord changed = *g_document->Find(id);
    changed.rgba ^= 0x00010101U;
    ++changed.content_revision;
    ChangeSet changes;
    CompileDiagnostics diagnostics;
    if (!g_document->Apply({OperationKind::kUpdate, id, changed}, &changes,
                           &error) ||
        !compiler.ApplyIncremental(*g_document, changes, g_scene.get(),
                                   &diagnostics, &error)) {
      return Failure(env, error);
    }
  }
  const RuntimeScene oracle = compiler.CompileFull(*g_document);
  if (oracle.Digest() != g_scene->Digest()) {
    return Failure(env, "incremental/full scene digest differs");
  }

  std::vector<double> frame_ms;
  frame_ms.reserve(static_cast<size_t>(frame_count));
  size_t maximum_candidates = 0U;
  size_t maximum_visible = 0U;
  uint64_t missed_intervals = 0U;
  const double interval_ms = refresh_rate > 0.0F ? 1000.0 / refresh_rate : 0.0;
  std::vector<uint8_t> incremental_rgba;
  for (int frame = 0; frame < frame_count; ++frame) {
    const float zoom = 0.75F + static_cast<float>(frame % 8) * 0.125F;
    const float pan_x = static_cast<float>((static_cast<uint64_t>(frame) * 37U)
                                           % 28000U);
    const float pan_y = static_cast<float>((static_cast<uint64_t>(frame) * 17U)
                                           % 2200U);
    const ViewState view{1U, static_cast<uint64_t>(frame) + 1U, 1U,
        Bounds{pan_x, pan_y,
               pan_x + static_cast<float>(g_width) / (zoom * g_dpr),
               pan_y + static_cast<float>(g_height) / (zoom * g_dpr)},
        zoom, g_dpr, g_width, g_height};
    const ViewQueryResult query = QueryView(*g_scene, view, std::nullopt);
    maximum_candidates = std::max(maximum_candidates, query.candidates.size());
    maximum_visible = std::max(maximum_visible, query.visible.size());
    double elapsed_ms = 0.0;
    const bool readback = frame == frame_count - 1;
    if (!g_adapter->Render(*g_scene, view, query, readback,
                           &incremental_rgba, &elapsed_ms, &error)) {
      return Failure(env, error);
    }
    frame_ms.push_back(elapsed_ms);
    if (interval_ms > 0.0 && elapsed_ms > interval_ms) ++missed_intervals;
  }

  const float final_pan_x =
      static_cast<float>(((frame_count - 1) * 37U) % 28000U);
  const float final_pan_y =
      static_cast<float>(((frame_count - 1) * 17U) % 2200U);
  const float final_zoom =
      0.75F + static_cast<float>((frame_count - 1) % 8) * 0.125F;
  const ViewState final_view{
      1U, static_cast<uint64_t>(frame_count), 1U,
      Bounds{final_pan_x, final_pan_y,
             final_pan_x + static_cast<float>(g_width) /
                               (g_dpr * final_zoom),
             final_pan_y + static_cast<float>(g_height) /
                               (g_dpr * final_zoom)},
      final_zoom, g_dpr, g_width, g_height};
  const ViewQueryResult oracle_query = QueryView(oracle, final_view, std::nullopt);
  std::vector<uint8_t> oracle_rgba;
  double oracle_ms = 0.0;
  if (!g_adapter->Render(oracle, final_view, oracle_query, true, &oracle_rgba,
                         &oracle_ms, &error)) {
    return Failure(env, error);
  }
  const bool visual_equivalent = incremental_rgba == oracle_rgba;
  g_pan_x = final_pan_x;
  g_pan_y = final_pan_y;
  g_zoom = final_zoom;

  const char* raw_path = env->GetStringUTFChars(output_path, nullptr);
  const std::string result_path(raw_path);
  env->ReleaseStringUTFChars(output_path, raw_path);
  std::ofstream output(result_path, std::ios::binary | std::ios::trunc);
  if (!output) return Failure(env, "result artifact could not be created");

  std::ostringstream result;
  result << std::fixed << std::setprecision(3)
         << "{\"schema_version\":1,\"platform\":\"android\","
         << "\"backend\":\"ganesh-gles3\",\"nodes\":100000,"
         << "\"document_digest\":\"" << g_document->Digest() << "\","
         << "\"scene_digest\":\"" << g_scene->Digest() << "\","
         << "\"full_incremental_equivalent\":true,"
         << "\"visual_equivalent\":"
         << (visual_equivalent ? "true" : "false") << ','
         << "\"frames\":" << frame_ms.size() << ','
         << "\"frame_p50_ms\":" << Percentile(frame_ms, 0.50) << ','
         << "\"frame_p95_ms\":" << Percentile(frame_ms, 0.95) << ','
         << "\"frame_p99_ms\":" << Percentile(frame_ms, 0.99) << ','
         << "\"frame_max_ms\":"
         << *std::max_element(frame_ms.begin(), frame_ms.end()) << ','
         << "\"refresh_rate_hz\":" << refresh_rate << ','
         << "\"surface_width_px\":" << g_width << ','
         << "\"surface_height_px\":" << g_height << ','
         << "\"dpr\":" << g_dpr << ','
         << "\"missed_presentations\":" << missed_intervals << ','
         << "\"maximum_candidates\":" << maximum_candidates << ','
         << "\"maximum_visible\":" << maximum_visible << ','
         << "\"process_peak_mib\":" << ProcessResidentMib() << ','
         << "\"input_events\":" << g_input_events << "}";
  output << result.str() << '\n';
  output.close();
  __android_log_print(ANDROID_LOG_INFO, "CanvasPOC03",
                      "CANVAS_POC03_RESULT %s", result.str().c_str());
  return env->NewStringUTF(result.str().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeTransform(
    JNIEnv* env, jobject, jfloat previous_focus_x, jfloat previous_focus_y,
    jfloat current_focus_x, jfloat current_focus_y, jfloat scale) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_adapter || !g_scene) {
    return Failure(env, "invalid interactive transform");
  }
  ViewportTransform transform{g_pan_x, g_pan_y, g_zoom};
  std::string error;
  if (!ApplyViewportGesture(
          ViewportGesture{previous_focus_x, previous_focus_y, current_focus_x,
                          current_focus_y, scale},
          g_dpr, 0.25F, 4.0F,
          Bounds{0.0F, 0.0F, 30000.0F, 3000.0F}, &transform, &error)) {
    return Failure(env, error);
  }
  g_pan_x = transform.pan_x;
  g_pan_y = transform.pan_y;
  g_zoom = transform.zoom;
  ++g_view_revision;
  ++g_input_events;
  const ViewState view = CurrentView();
  const ViewQueryResult query = QueryView(*g_scene, view, std::nullopt);
  std::vector<uint8_t> ignored;
  double elapsed_ms = 0.0;
  if (!g_adapter->Render(*g_scene, view, query, false, &ignored, &elapsed_ms,
                         &error)) {
    return Failure(env, error);
  }
  const std::string result = "INTERACTIVE " + std::to_string(elapsed_ms) +
                             "ms candidates=" +
                             std::to_string(query.candidates.size());
  return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeDetach(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_adapter.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeDestroy(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_adapter.reset();
  g_scene.reset();
  g_document.reset();
}
