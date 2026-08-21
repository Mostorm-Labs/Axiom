#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "android_gles_adapter.h"
#include "canvas/poc03/large_scene.h"
#include "canvas/poc05/hybrid_surface.h"

namespace {

using canvas::poc03::AndroidGlesAdapter;
using canvas::poc03::Bounds;
using canvas::poc03::Document;
using canvas::poc03::QueryView;
using canvas::poc03::RuntimeScene;
using canvas::poc03::SceneCompiler;
using canvas::poc03::ViewState;
using canvas::poc03::ViewportGesture;
using canvas::poc03::ViewportTransform;
using canvas::poc05::ExternalSurfaceId;
using canvas::poc05::ExternalSurfacePlaceholder;
using canvas::poc05::ExternalSurfaceRegistry;
using canvas::poc05::PlacementCommand;
using canvas::poc05::PlatformOverlayBackend;
using canvas::poc05::RuntimeViewFrame;
using canvas::poc05::RuntimeViewProjector;
using canvas::poc05::SurfaceKind;

constexpr CanvasViewHandle kExperimentalViewHandle = 1U;
constexpr std::size_t kPlacementStride = 12U;

std::mutex g_mutex;
std::unique_ptr<Document> g_document;
std::unique_ptr<RuntimeScene> g_scene;
std::unique_ptr<AndroidGlesAdapter> g_adapter;
std::uint32_t g_width = 0U;
std::uint32_t g_height = 0U;
std::uint32_t g_base_nodes = 100000U;
float g_pan_x = 0.0F;
float g_pan_y = 0.0F;
float g_zoom = 1.0F;
float g_dpr = 1.0F;
std::uint64_t g_view_revision = 1U;

class AndroidProjector final : public RuntimeViewProjector {
 public:
  bool worldToViewLogical(CanvasViewHandle view, CanvasPointF world_point,
                          CanvasPointF* logical_point,
                          std::string* error) const override {
    if (view != kExperimentalViewHandle || logical_point == nullptr ||
        error == nullptr || !std::isfinite(world_point.x) ||
        !std::isfinite(world_point.y)) {
      if (error != nullptr) *error = "invalid Android projection request";
      return false;
    }
    logical_point->x = (world_point.x - g_pan_x) * g_zoom;
    logical_point->y = (world_point.y - g_pan_y) * g_zoom;
    return true;
  }
};

class AndroidPlacementBackend final : public PlatformOverlayBackend {
 public:
  bool create(ExternalSurfaceId id, SurfaceKind kind,
              std::string* error) override {
    if (id == 0U || commands_.contains(id)) {
      if (error != nullptr) *error = "duplicate Android overlay";
      return false;
    }
    kinds_[id] = kind;
    commands_[id] = PlacementCommand{};
    return true;
  }

  bool apply(const PlacementCommand& command, std::string* error) override {
    if (!commands_.contains(command.id)) {
      if (error != nullptr) *error = "Android overlay is not materialized";
      return false;
    }
    commands_[command.id] = command;
    return true;
  }

  void destroy(ExternalSurfaceId id) override {
    commands_.erase(id);
    kinds_.erase(id);
  }

  bool focus(ExternalSurfaceId id, std::string* error) override {
    if (!commands_.contains(id)) {
      if (error != nullptr) *error = "Android overlay is not focusable";
      return false;
    }
    focused_ = id;
    return true;
  }

  void focusCanvas() override { focused_ = 0U; }

  [[nodiscard]] const PlacementCommand* find(ExternalSurfaceId id) const {
    const auto found = commands_.find(id);
    return found == commands_.end() ? nullptr : &found->second;
  }

 private:
  std::unordered_map<ExternalSurfaceId, SurfaceKind> kinds_;
  std::unordered_map<ExternalSurfaceId, PlacementCommand> commands_;
  ExternalSurfaceId focused_ = 0U;
};

AndroidProjector g_projector;
AndroidPlacementBackend g_backend;
std::unique_ptr<ExternalSurfaceRegistry> g_registry;

ViewState CurrentView() {
  const float world_width = static_cast<float>(g_width) / (g_zoom * g_dpr);
  const float world_height = static_cast<float>(g_height) / (g_zoom * g_dpr);
  return ViewState{kExperimentalViewHandle, g_view_revision, 1U,
                   Bounds{g_pan_x, g_pan_y, g_pan_x + world_width,
                          g_pan_y + world_height},
                   g_zoom, g_dpr, g_width, g_height};
}

bool EnsureScene(std::uint32_t base_nodes, std::string* error) {
  try {
    if (base_nodes != 1000U && base_nodes != 10000U &&
        base_nodes != 50000U && base_nodes != 100000U) {
      *error = "Android POC-05 scale must be 1K, 10K, 50K, or 100K";
      return false;
    }
    if (!g_document) {
      g_base_nodes = base_nodes;
      g_document = std::make_unique<Document>(canvas::poc03::GenerateDocument(
          {base_nodes, UINT64_C(0x43414e5641533035), 1000U, 32.0F}));
      g_scene = std::make_unique<RuntimeScene>(
          SceneCompiler().CompileFull(*g_document));
    } else if (base_nodes != g_base_nodes) {
      *error = "Android POC-05 scale cannot change while attached";
      return false;
    }
    return true;
  } catch (const std::exception& exception) {
    *error = exception.what();
    return false;
  }
}

bool Render(std::string* error) {
  if (!g_adapter || !g_scene) {
    *error = "Android Canvas surface is detached";
    return false;
  }
  const ViewState view = CurrentView();
  const auto query = QueryView(*g_scene, view, std::nullopt);
  std::vector<std::uint8_t> ignored;
  double elapsed_ms = 0.0;
  return g_adapter->Render(*g_scene, view, query, false, &ignored,
                           &elapsed_ms, error);
}

jstring JniString(JNIEnv* env, const std::string& value) {
  return env->NewStringUTF(value.c_str());
}

jstring Failure(JNIEnv* env, const std::string& message) {
  const std::string result = "FAIL " + message;
  __android_log_print(ANDROID_LOG_ERROR, "AxiomPOC05", "%s",
                      result.c_str());
  return JniString(env, result);
}

ExternalSurfacePlaceholder WebPlaceholder() {
  ExternalSurfacePlaceholder placeholder;
  placeholder.id = 1U;
  placeholder.kind = SurfaceKind::kWebView;
  placeholder.worldBounds = CanvasRectF{180.0F, 120.0F, 380.0F, 230.0F};
  placeholder.worldClip = CanvasRectF{190.0F, 130.0F, 360.0F, 210.0F};
  placeholder.order = 1U;
  placeholder.pageId = 1U;
  return placeholder;
}

ExternalSurfacePlaceholder VideoPlaceholder() {
  ExternalSurfacePlaceholder placeholder;
  placeholder.id = 2U;
  placeholder.kind = SurfaceKind::kVideo;
  placeholder.worldBounds = CanvasRectF{680.0F, 300.0F, 320.0F, 180.0F};
  placeholder.order = 2U;
  placeholder.pageId = 1U;
  return placeholder;
}

void AppendPlacement(const PlacementCommand* command,
                     std::vector<float>* values) {
  if (command == nullptr) {
    values->insert(values->end(), kPlacementStride, 0.0F);
    return;
  }
  values->push_back(command->deviceBounds.x);
  values->push_back(command->deviceBounds.y);
  values->push_back(command->deviceBounds.width);
  values->push_back(command->deviceBounds.height);
  values->push_back(command->relativeDeviceClip.x);
  values->push_back(command->relativeDeviceClip.y);
  values->push_back(command->relativeDeviceClip.width);
  values->push_back(command->relativeDeviceClip.height);
  values->push_back(command->visible ? 1.0F : 0.0F);
  values->push_back(command->contentVisible ? 1.0F : 0.0F);
  values->push_back(command->failurePlaceholder ? 1.0F : 0.0F);
  values->push_back(command->opacity);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_axiom_poc05_AxiomCanvasSurfaceView_nativeAttach(
    JNIEnv* env, jobject, jobject surface, jint width, jint height,
    jfloat density, jint base_nodes) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (width <= 0 || height <= 0 || !std::isfinite(density) || density <= 0.0F) {
    return Failure(env, "invalid dimensions or density");
  }
  std::string error;
  if (!EnsureScene(static_cast<std::uint32_t>(base_nodes), &error)) {
    return Failure(env, error);
  }
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) return Failure(env, "native window unavailable");
  g_adapter = std::make_unique<AndroidGlesAdapter>();
  const bool attached = g_adapter->Attach(
      window, static_cast<std::uint32_t>(width),
      static_cast<std::uint32_t>(height), &error);
  ANativeWindow_release(window);
  if (!attached) return Failure(env, error);
  g_width = static_cast<std::uint32_t>(width);
  g_height = static_cast<std::uint32_t>(height);
  g_dpr = density;
  if (!Render(&error)) return Failure(env, error);
  return JniString(env, "ATTACHED react-native-fabric ganesh-gles3 " +
                            std::to_string(g_base_nodes));
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_axiom_poc05_AxiomCanvasSurfaceView_nativeConfigureSurfaces(
    JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_registry = std::make_unique<ExternalSurfaceRegistry>(g_projector, g_backend);
  std::string error;
  if (!g_registry->registerSurface(WebPlaceholder(), &error) ||
      !g_registry->registerSurface(VideoPlaceholder(), &error) ||
      !g_registry->markReady(1U, &error) ||
      !g_registry->markReady(2U, &error)) {
    return Failure(env, error);
  }
  return JniString(env, "EXTERNAL_SURFACES_CONFIGURED");
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_dev_mostorm_axiom_poc05_AxiomCanvasSurfaceView_nativePlacements(
    JNIEnv* env, jobject, jlong frame_revision, jlong viewport_revision,
    jint target_generation) {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<float> values;
  values.reserve(kPlacementStride * 2U);
  if (!g_registry || frame_revision <= 0 || viewport_revision <= 0 ||
      target_generation <= 0) {
    values.resize(kPlacementStride * 2U, 0.0F);
  } else {
    CanvasCameraStateV1 camera{};
    camera.struct_size = sizeof(camera);
    camera.abi_version = CANVAS_RUNTIME_ABI_VERSION;
    camera.scale = g_zoom;
    camera.world_origin_x = g_pan_x;
    camera.world_origin_y = g_pan_y;
    camera.viewport_revision = static_cast<std::uint64_t>(viewport_revision);
    CanvasSurfaceStateV1 surface{};
    surface.struct_size = sizeof(surface);
    surface.abi_version = CANVAS_RUNTIME_ABI_VERSION;
    surface.width_pixels = g_width;
    surface.height_pixels = g_height;
    surface.device_pixel_ratio = g_dpr;
    surface.target_generation = static_cast<std::uint32_t>(target_generation);
    surface.color_space = kCanvasColorSpaceSrgb;
    surface.orientation = kCanvasSurfaceOrientationIdentity;
    std::string error;
    const RuntimeViewFrame frame{kExperimentalViewHandle, camera, surface,
                                 static_cast<std::uint64_t>(frame_revision)};
    if (!g_registry->applyFrame(frame, &error)) {
      __android_log_print(ANDROID_LOG_ERROR, "AxiomPOC05",
                          "placement failed: %s", error.c_str());
      values.resize(kPlacementStride * 2U, 0.0F);
    } else {
      AppendPlacement(g_backend.find(1U), &values);
      AppendPlacement(g_backend.find(2U), &values);
    }
  }
  jfloatArray result = env->NewFloatArray(static_cast<jsize>(values.size()));
  if (result != nullptr) {
    env->SetFloatArrayRegion(result, 0, static_cast<jsize>(values.size()),
                             values.data());
  }
  return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_axiom_poc05_AxiomCanvasSurfaceView_nativeTransform(
    JNIEnv* env, jobject, jfloat previous_focus_x, jfloat previous_focus_y,
    jfloat current_focus_x, jfloat current_focus_y, jfloat scale) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_adapter || !g_scene) return Failure(env, "invalid transform state");
  ViewportTransform transform{g_pan_x, g_pan_y, g_zoom};
  std::string error;
  if (!canvas::poc03::ApplyViewportGesture(
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
  if (!Render(&error)) return Failure(env, error);
  return JniString(env, "INTERACTIVE");
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_axiom_poc05_AxiomCanvasSurfaceView_nativeRunCorpus(
    JNIEnv* env, jobject, jstring output_path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_document || !g_scene || !g_registry) {
    return Failure(env, "native corpus is not configured");
  }
  const RuntimeScene oracle = SceneCompiler().CompileFull(*g_document);
  if (oracle.Digest() != g_scene->Digest()) {
    return Failure(env, "Document to RuntimeScene digest differs");
  }
  const auto& diagnostics = g_registry->diagnostics();
  std::ostringstream result;
  result << "{\"schema_version\":1,\"nodes\":" << g_base_nodes
         << ",\"scene_equivalent\":true,\"active_surfaces\":"
         << diagnostics.activeSurfaceCount
         << ",\"runtime_c_abi_binary_conformance\":false}";
  const char* raw_path = env->GetStringUTFChars(output_path, nullptr);
  const std::string path(raw_path);
  env->ReleaseStringUTFChars(output_path, raw_path);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return Failure(env, "native corpus artifact could not be written");
  output << result.str() << '\n';
  __android_log_print(ANDROID_LOG_INFO, "AxiomPOC05",
                      "CANVAS_POC05_NATIVE_RESULT %s", result.str().c_str());
  return JniString(env, result.str());
}

extern "C" JNIEXPORT void JNICALL
Java_dev_mostorm_axiom_poc05_AxiomCanvasSurfaceView_nativeDetach(
    JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_adapter.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_mostorm_axiom_poc05_AxiomCanvasSurfaceView_nativeDestroy(
    JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_registry.reset();
  g_adapter.reset();
  g_scene.reset();
  g_document.reset();
}
