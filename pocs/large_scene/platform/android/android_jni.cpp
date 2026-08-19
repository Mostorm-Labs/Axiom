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
#include "canvas/poc03/ink_integration.h"

namespace {

using namespace canvas::poc03;
namespace poc02 = canvas::poc02;

std::mutex g_mutex;
std::unique_ptr<Document> g_document;
std::unique_ptr<RuntimeScene> g_scene;
std::unique_ptr<InkGeometryStore> g_ink_geometry;
std::unique_ptr<TileCache> g_tile_cache;
std::unique_ptr<DeterministicFrameScheduler> g_scheduler;
std::unique_ptr<IntegratedInkController> g_ink_controller;
std::unique_ptr<AndroidGlesAdapter> g_adapter;
uint32_t g_width = 0;
uint32_t g_height = 0;
uint32_t g_base_nodes = 100000U;
uint32_t g_historical_strokes = 20000U;
float g_pan_x = 0.0F;
float g_pan_y = 0.0F;
float g_zoom = 1.0F;
float g_dpr = 1.0F;
uint64_t g_view_revision = 1U;
uint64_t g_input_events = 0U;
uint64_t g_live_stroke_id = UINT64_C(0x8000000000000000);
uint64_t g_live_sample_sequence = 0U;
uint32_t g_live_order = 120002U;
uint64_t g_selected_node_id = 0U;
Bounds g_drag_original_bounds;
float g_drag_offset_x = 0.0F;
float g_drag_offset_y = 0.0F;

double Percentile(std::vector<double> values, double percentile) {
  std::sort(values.begin(), values.end());
  if (values.empty()) return 0.0;
  const size_t index = static_cast<size_t>(
      std::ceil(percentile * static_cast<double>(values.size())) - 1.0);
  return values[std::min(index, values.size() - 1U)];
}

double ProcessStatusMib(const char* requested_key) {
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == requested_key) {
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

bool IsSupportedScale(uint32_t nodes) {
  return nodes == 1000U || nodes == 10000U || nodes == 50000U ||
         nodes == 100000U;
}

bool EnsureScene(uint32_t base_nodes, std::string* error) {
  try {
    if (!g_document) {
      if (!IsSupportedScale(base_nodes)) {
        *error = "Android POC-03 scale must be 1K, 10K, 50K, or 100K";
        return false;
      }
      g_base_nodes = base_nodes;
      g_historical_strokes = base_nodes / 5U;
      g_live_order = g_base_nodes + g_historical_strokes + 2U;
      g_document = std::make_unique<Document>();
      g_scene = std::make_unique<RuntimeScene>();
      g_ink_geometry = std::make_unique<InkGeometryStore>();
      g_tile_cache = std::make_unique<TileCache>(64U * 1024U * 1024U);
      g_scheduler = std::make_unique<DeterministicFrameScheduler>();
      IntegratedScaleReport scale;
      if (!BuildIntegratedScale({g_base_nodes, g_historical_strokes,
                                 0x43414e5641533033ULL},
                                g_document.get(), g_scene.get(),
                                g_ink_geometry.get(), g_tile_cache.get(),
                                g_scheduler.get(),
                                &scale, error)) {
        return false;
      }
      IntegratedActionReport actions;
      if (!RunIntegratedActionCycle(g_base_nodes, g_historical_strokes,
                                    g_document.get(),
                                    g_scene.get(), g_ink_geometry.get(),
                                    g_tile_cache.get(), g_scheduler.get(),
                                    &actions, error)) {
        return false;
      }
      g_ink_controller = std::make_unique<IntegratedInkController>(
          *g_ink_geometry, *g_document, *g_scene, *g_tile_cache,
          *g_scheduler);
    } else if (g_base_nodes != base_nodes) {
      *error = "Android scale cannot change while the surface is attached";
      return false;
    }
    return true;
  } catch (const std::exception& exception) {
    *error = exception.what();
    return false;
  }
}

poc02::PointerSampleBatch AndroidInkBatch(
    jlong pointer_id, jint action, const std::vector<float>& xs,
    const std::vector<float>& ys, const std::vector<float>& pressures,
    const std::vector<int64_t>& timestamps_ms) {
  poc02::PointerSampleBatch batch{
      .view_id = 1U,
      .viewport_revision = g_view_revision,
      .view_to_world = {
          .m00 = 1.0F / (g_zoom * g_dpr),
          .m11 = 1.0F / (g_zoom * g_dpr),
          .tx = g_pan_x,
          .ty = g_pan_y,
      },
      .device = {
          .device_id = static_cast<uint64_t>(pointer_id),
          .tool = poc02::PointerTool::kPen,
          .capabilities = poc02::kCapabilityPressure |
                          poc02::kCapabilityContact,
      },
      .samples = {},
  };
  batch.samples.reserve(xs.size());
  for (size_t index = 0U; index < xs.size(); ++index) {
    poc02::PointerPhase phase = poc02::PointerPhase::kMove;
    if (action == 0 && index == 0U) {
      phase = poc02::PointerPhase::kDown;
    } else if (action == 2 && index + 1U == xs.size()) {
      phase = poc02::PointerPhase::kUp;
    }
    batch.samples.push_back(poc02::PointerSample{
        .pointer_id = static_cast<uint64_t>(pointer_id),
        .sample_sequence = g_live_sample_sequence++,
        .position = {xs[index], ys[index]},
        .pressure = std::clamp(pressures[index], 0.0F, 1.0F),
        .tilt = {},
        .contact_size = {2.0F, 2.0F},
        .timestamp_us = static_cast<uint64_t>(timestamps_ms[index]) * 1000U,
        .phase = phase,
    });
  }
  return batch;
}

bool RenderInteractive(const poc02::DefaultPreviewSink::State* preview,
                       std::string* error) {
  const ViewState view = CurrentView();
  const ViewQueryResult query = QueryView(*g_scene, view, std::nullopt);
  std::vector<uint8_t> ignored;
  double elapsed_ms = 0.0;
  return g_adapter->Render(*g_scene, view, query, false, &ignored, &elapsed_ms,
                           error, g_ink_geometry.get(), preview);
}

bool ApplyNodeDrag(float screen_x, float screen_y, std::string* error) {
  if (g_selected_node_id == 0U || !g_document || !g_scene) return true;
  const float world_x = g_pan_x + screen_x / (g_zoom * g_dpr);
  const float world_y = g_pan_y + screen_y / (g_zoom * g_dpr);
  const NodeRecord* current = g_document->Find(g_selected_node_id);
  if (current == nullptr) return false;
  NodeRecord moved = *current;
  const float width = g_drag_original_bounds.right - g_drag_original_bounds.left;
  const float height = g_drag_original_bounds.bottom - g_drag_original_bounds.top;
  moved.bounds = Bounds{world_x - g_drag_offset_x, world_y - g_drag_offset_y,
                        world_x - g_drag_offset_x + width,
                        world_y - g_drag_offset_y + height};
  if (!moved.bounds.IsFiniteAndOrdered()) return false;
  ChangeSet changes;
  SceneCompiler compiler;
  CompileDiagnostics diagnostics;
  if (!g_document->Apply({OperationKind::kUpdate, moved.id, moved}, &changes,
                         error) ||
      !compiler.ApplyIncremental(*g_document, changes, g_scene.get(),
                                 &diagnostics, error)) {
    return false;
  }
  // Hints are disposable. The compiler's authoritative dirty bounds remain
  // valid even when a producer omits or corrupts its optional hint.
  g_tile_cache->InvalidateWorld(1U, diagnostics.authoritative_world_dirty,
                                256.0F);
  return true;
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
    jfloat density, jint base_nodes) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (width <= 0 || height <= 0 || !std::isfinite(density) || density <= 0.0F) {
    return Failure(env, "invalid dimensions or density");
  }
  std::string error;
  if (!EnsureScene(static_cast<uint32_t>(base_nodes), &error)) {
    return Failure(env, error);
  }
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
  return env->NewStringUTF((std::string("ATTACHED ganesh-gles3 ") +
                            std::to_string(g_base_nodes)).c_str());
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
  const RuntimeScene oracle = compiler.CompileFull(*g_document);
  if (oracle.Digest() != g_scene->Digest()) {
    return Failure(env, "incremental/full scene digest differs");
  }

  constexpr int kWarmupFrames = 1800;
  const auto make_view = [&](int frame) {
    const float zoom = 0.75F + static_cast<float>(frame % 8) * 0.125F;
    const float pan_x = static_cast<float>(
        (static_cast<uint64_t>(frame) * 37U) % 28000U);
    const float pan_y = static_cast<float>(
        (static_cast<uint64_t>(frame) * 17U) % 2200U);
    return ViewState{
        1U,
        static_cast<uint64_t>(frame) + 1U,
        1U,
        Bounds{pan_x, pan_y,
               pan_x + static_cast<float>(g_width) / (zoom * g_dpr),
               pan_y + static_cast<float>(g_height) / (zoom * g_dpr)},
        zoom,
        g_dpr,
        g_width,
        g_height,
    };
  };
  std::vector<double> frame_ms;
  frame_ms.reserve(static_cast<size_t>(frame_count));
  std::vector<uint8_t> incremental_rgba;
  double warmup_ms = 0.0;
  for (int frame = 0; frame < kWarmupFrames; ++frame) {
    const ViewState view = make_view(frame);
    const ViewQueryResult query = QueryView(*g_scene, view, std::nullopt);
    if (!g_adapter->Render(*g_scene, view, query,
                           frame == kWarmupFrames - 1, &incremental_rgba,
                           &warmup_ms, &error, g_ink_geometry.get())) {
      return Failure(env, error);
    }
  }
  const ViewState warmup_final_view = make_view(kWarmupFrames - 1);
  const ViewQueryResult warmup_oracle_query =
      QueryView(oracle, warmup_final_view, std::nullopt);
  if (!g_adapter->Render(oracle, warmup_final_view, warmup_oracle_query, true,
                         &incremental_rgba, &warmup_ms, &error,
                         g_ink_geometry.get())) {
    return Failure(env, error);
  }
  const double process_start_mib = ProcessStatusMib("VmRSS:");
  const auto trace_start = std::chrono::steady_clock::now();

  size_t maximum_candidates = 0U;
  size_t maximum_visible = 0U;
  uint64_t missed_intervals = 0U;
  const double interval_ms = refresh_rate > 0.0F ? 1000.0 / refresh_rate : 0.0;
  for (int frame = 0; frame < frame_count; ++frame) {
    const ViewState view = make_view(frame);
    const ViewQueryResult query = QueryView(*g_scene, view, std::nullopt);
    maximum_candidates = std::max(maximum_candidates, query.candidates.size());
    maximum_visible = std::max(maximum_visible, query.visible.size());
    double elapsed_ms = 0.0;
    const bool readback = frame == frame_count - 1;
    if (!g_adapter->Render(*g_scene, view, query, readback,
                           &incremental_rgba, &elapsed_ms, &error,
                           g_ink_geometry.get())) {
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
                         &oracle_ms, &error, g_ink_geometry.get())) {
    return Failure(env, error);
  }
  const bool visual_equivalent = incremental_rgba == oracle_rgba;
  const auto trace_end = std::chrono::steady_clock::now();
  const double process_end_mib = ProcessStatusMib("VmRSS:");
  const double trace_wall_ms =
      std::chrono::duration<double, std::milli>(trace_end - trace_start).count();
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
         << "\"backend\":\"ganesh-gles3\",\"nodes\":"
         << g_base_nodes << ","
         << "\"historical_strokes\":" << g_historical_strokes << ","
         << "\"ink_document_digest\":\""
         << g_ink_geometry->document().Digest() << "\","
         << "\"document_digest\":\"" << g_document->Digest() << "\","
         << "\"scene_digest\":\"" << g_scene->Digest() << "\","
         << "\"full_incremental_equivalent\":true,"
         << "\"visual_equivalent\":"
         << (visual_equivalent ? "true" : "false") << ','
         << "\"warmup_frames\":" << kWarmupFrames << ','
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
         << "\"trace_wall_ms\":" << trace_wall_ms << ','
         << "\"process_start_mib\":" << process_start_mib << ','
         << "\"process_end_mib\":" << process_end_mib << ','
         << "\"process_steady_growth_percent\":"
         << (process_start_mib > 0.0
                 ? ((process_end_mib - process_start_mib) / process_start_mib) *
                       100.0
                 : 0.0)
         << ','
         << "\"process_peak_mib\":" << ProcessStatusMib("VmHWM:") << ','
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
                         &error, g_ink_geometry.get())) {
    return Failure(env, error);
  }
  const std::string result = "INTERACTIVE " + std::to_string(elapsed_ms) +
                             "ms candidates=" +
                             std::to_string(query.candidates.size());
  return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeSelectBegin(
    JNIEnv* env, jobject, jfloat screen_x, jfloat screen_y) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_adapter || !g_scene || !g_document) {
    return Failure(env, "invalid selection state");
  }
  const float world_x = g_pan_x + screen_x / (g_zoom * g_dpr);
  const float world_y = g_pan_y + screen_y / (g_zoom * g_dpr);
  const auto hits = HitTest(*g_scene, CurrentView(), world_x, world_y,
                            12.0F / (g_zoom * g_dpr));
  const auto selected = SelectFirstUnlocked(*g_scene, hits);
  if (!selected) {
    g_selected_node_id = 0U;
    return env->NewStringUTF("SELECT_NONE");
  }
  const NodeRecord* node = g_document->Find(*selected);
  if (node == nullptr || node->type == NodeType::kStroke) {
    g_selected_node_id = 0U;
    return env->NewStringUTF("SELECT_NONE");
  }
  g_selected_node_id = *selected;
  g_drag_original_bounds = node->bounds;
  g_drag_offset_x = world_x - node->bounds.left;
  g_drag_offset_y = world_y - node->bounds.top;
  return env->NewStringUTF((std::string("SELECTED ") +
                            std::to_string(*selected)).c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeSelectMove(
    JNIEnv* env, jobject, jfloat screen_x, jfloat screen_y) {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string error;
  if (!ApplyNodeDrag(screen_x, screen_y, &error)) return Failure(env, error);
  if (!RenderInteractive(nullptr, &error)) return Failure(env, error);
  return env->NewStringUTF("SELECT_DRAGGING");
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeSelectEnd(
    JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_selected_node_id = 0U;
  return env->NewStringUTF("SELECT_END");
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc03_CanvasPoc03View_nativeInkBatch(
    JNIEnv* env, jobject, jint brush_type, jint action, jlong pointer_id,
    jfloatArray x_values, jfloatArray y_values, jfloatArray pressure_values,
    jlongArray timestamp_values) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_adapter || !g_ink_controller || pointer_id <= 0 ||
      (brush_type != 1 && brush_type != 2)) {
    return Failure(env, "invalid live Ink state");
  }
  const jsize count = env->GetArrayLength(x_values);
  if (count <= 0 || env->GetArrayLength(y_values) != count ||
      env->GetArrayLength(pressure_values) != count ||
      env->GetArrayLength(timestamp_values) != count) {
    return Failure(env, "Ink arrays must have identical positive lengths");
  }
  std::vector<float> xs(static_cast<size_t>(count));
  std::vector<float> ys(static_cast<size_t>(count));
  std::vector<float> pressures(static_cast<size_t>(count));
  std::vector<int64_t> timestamps(static_cast<size_t>(count));
  env->GetFloatArrayRegion(x_values, 0, count, xs.data());
  env->GetFloatArrayRegion(y_values, 0, count, ys.data());
  env->GetFloatArrayRegion(pressure_values, 0, count, pressures.data());
  env->GetLongArrayRegion(timestamp_values, 0, count,
                          reinterpret_cast<jlong*>(timestamps.data()));
  std::string error;
  if (action == 3) {
    const poc02::Status status = g_ink_controller->Cancel();
    return env->NewStringUTF(status == poc02::Status::kOk
                                 ? "INK_CANCELLED"
                                 : "INK_CANCEL_IGNORED");
  }
  if (action == 0) {
    g_live_sample_sequence = 0U;
  }
  poc02::PointerSampleBatch batch = AndroidInkBatch(
      pointer_id, action, xs, ys, pressures, timestamps);
  poc02::Status status = poc02::Status::kOk;
  if (action == 0) {
    status = g_ink_controller->Begin(
        ++g_live_stroke_id, static_cast<uint64_t>(pointer_id),
        DeterministicBrush(brush_type == 1 ? poc02::BrushType::kVector
                                           : poc02::BrushType::kDab),
        batch);
  } else {
    status = g_ink_controller->Push(
        std::move(batch), static_cast<uint64_t>(timestamps.back()) * 1000U);
  }
  if (status != poc02::Status::kOk) {
    return Failure(env, "Ink input failed: " +
                            std::string(poc02::StatusName(status)));
  }
  ++g_input_events;
  if (action != 2) {
    const auto* preview = g_ink_controller->DrawablePreview(
        g_scene->source_revision());
    if (!RenderInteractive(preview, &error)) return Failure(env, error);
    return env->NewStringUTF("INK_PREVIEW");
  }
  InkCommitDiagnostics diagnostics;
  if (!g_ink_controller->Commit(g_live_order++, &diagnostics, &error)) {
    return Failure(env, error);
  }
  if (!RenderInteractive(nullptr, &error)) return Failure(env, error);
  const auto frame = g_ink_controller->BeginFrame();
  if (!frame || !g_ink_controller->CompletePresentation(
                    *frame, g_scene->source_revision(), true, &diagnostics,
                    &error)) {
    return Failure(env, error.empty() ? "visible acknowledgement failed" : error);
  }
  return env->NewStringUTF("INK_CANONICAL_VISIBLE");
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
  g_ink_controller.reset();
  g_scene.reset();
  g_ink_geometry.reset();
  g_scheduler.reset();
  g_tile_cache.reset();
  g_document.reset();
  g_selected_node_id = 0U;
}
