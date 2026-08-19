#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "skia_large_scene_renderer.h"
#include "canvas/poc03/ink_integration.h"

namespace {

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl = 0;
sk_sp<GrDirectContext> g_context;
sk_sp<SkSurface> g_surface;
std::string g_result;
std::unique_ptr<canvas::poc03::Document> g_document;
std::unique_ptr<canvas::poc03::RuntimeScene> g_scene;
std::unique_ptr<canvas::poc03::RuntimeScene> g_oracle;
std::unique_ptr<canvas::poc03::InkGeometryStore> g_ink_geometry;
std::unique_ptr<canvas::poc03::TileCache> g_tile_cache;
std::unique_ptr<canvas::poc03::DeterministicFrameScheduler> g_scheduler;
std::unique_ptr<canvas::poc03::IntegratedInkController> g_ink_controller;
uint64_t g_live_sample_sequence = 0U;
uint64_t g_live_stroke_id = UINT64_C(0x8000000000000000);
uint32_t g_live_order = 120002U;
uint32_t g_base_nodes = 100000U;
uint32_t g_historical_strokes = 20000U;
size_t g_maximum_candidates = 0U;
size_t g_maximum_visible = 0U;
float g_pan_x = 0.0F;
float g_pan_y = 0.0F;
float g_zoom = 1.0F;
uint64_t g_view_revision = 1U;
uint64_t g_target_generation = 1U;
uint64_t g_selected_node_id = 0U;
canvas::poc03::Bounds g_drag_original_bounds;
float g_drag_offset_x = 0.0F;
float g_drag_offset_y = 0.0F;

void Reset() {
  g_surface.reset();
  if (g_context) {
    g_context->abandonContext();
    g_context.reset();
  }
  if (g_webgl != 0) {
    emscripten_webgl_destroy_context(g_webgl);
    g_webgl = 0;
  }
  g_ink_controller.reset();
  g_oracle.reset();
  g_scene.reset();
  g_document.reset();
  g_ink_geometry.reset();
  g_scheduler.reset();
  g_tile_cache.reset();
  g_maximum_candidates = 0U;
  g_maximum_visible = 0U;
  g_pan_x = 0.0F;
  g_pan_y = 0.0F;
  g_zoom = 1.0F;
  g_view_revision = 1U;
  g_target_generation = 1U;
  g_selected_node_id = 0U;
  g_result.clear();
}

bool EnsureSurface(const char *selector) {
  if (g_surface) {
    return true;
  }
  if (selector == nullptr ||
      emscripten_set_canvas_element_size(selector, 1280, 720) !=
          EMSCRIPTEN_RESULT_SUCCESS) {
    g_result = "{\"error\":\"canvas sizing failed\"}";
    return false;
  }
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = true;
  attributes.antialias = false;
  attributes.depth = false;
  attributes.stencil = true;
  attributes.premultipliedAlpha = true;
  attributes.preserveDrawingBuffer = true;
  attributes.majorVersion = 2;
  attributes.enableExtensionsByDefault = false;
  g_webgl = emscripten_webgl_create_context(selector, &attributes);
  if (g_webgl <= 0) {
    g_result =
        "{\"error\":\"WebGL2 context creation failed\",\"emscripten_result\":" +
        std::to_string(g_webgl) + "}";
    return false;
  }
  const EMSCRIPTEN_RESULT make_current =
      emscripten_webgl_make_context_current(g_webgl);
  if (make_current != EMSCRIPTEN_RESULT_SUCCESS) {
    g_result =
        "{\"error\":\"WebGL2 make-current failed\",\"emscripten_result\":" +
        std::to_string(make_current) + "}";
    return false;
  }
  sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
  if (!interface) {
    g_result = "{\"error\":\"Skia native GL interface creation failed\"}";
    return false;
  }
  g_context = GrDirectContexts::MakeGL(interface);
  if (!g_context) {
    g_result = "{\"error\":\"Skia Ganesh GL context creation failed\"}";
    return false;
  }
  GLint framebuffer_id = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_id);
  GrGLFramebufferInfo framebuffer{};
  framebuffer.fFBOID = static_cast<GrGLuint>(framebuffer_id);
  framebuffer.fFormat = static_cast<GrGLenum>(GL_RGBA8);
  const GrBackendRenderTarget target =
      GrBackendRenderTargets::MakeGL(1280, 720, 1, 8, framebuffer);
  g_surface = SkSurfaces::WrapBackendRenderTarget(
      g_context.get(), target, kBottomLeft_GrSurfaceOrigin,
      kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
  if (!g_surface) {
    g_result = "{\"error\":\"Skia WebGL2 surface wrapping failed\"}";
    return false;
  }
  return true;
}

canvas::poc03::ViewState TraceView(uint32_t frame) {
  using namespace canvas::poc03;
  const uint32_t trace_frame = frame % 600U;
  const float zoom = 0.75F + static_cast<float>(trace_frame % 8U) * 0.125F;
  const float pan_x =
      static_cast<float>((static_cast<uint64_t>(trace_frame) * 37U) % 28000U);
  const float pan_y =
      static_cast<float>((static_cast<uint64_t>(trace_frame) * 17U) % 2200U);
  return ViewState{
      1U,
      static_cast<uint64_t>(frame) + 1U,
      1U,
      Bounds{pan_x, pan_y, pan_x + 1280.0F / zoom, pan_y + 720.0F / zoom},
      zoom,
      1.0F,
      1280U,
      720U};
}

bool Prepare(uint32_t base_nodes = 100000U) {
  using namespace canvas::poc03;
  if (base_nodes != 1000U && base_nodes != 10000U &&
      base_nodes != 50000U && base_nodes != 100000U) {
    g_result = "{\"error\":\"scale must be 1K, 10K, 50K, or 100K\"}";
    return false;
  }
  if (!EnsureSurface("#scene")) {
    return false;
  }
  g_document = std::make_unique<Document>();
  g_scene = std::make_unique<RuntimeScene>();
  g_ink_geometry = std::make_unique<InkGeometryStore>();
  g_tile_cache = std::make_unique<TileCache>(64U * 1024U * 1024U);
  g_scheduler = std::make_unique<DeterministicFrameScheduler>();
  IntegratedScaleReport scale;
  std::string error;
  g_base_nodes = base_nodes;
  g_historical_strokes = base_nodes / 5U;
  g_live_order = g_base_nodes + g_historical_strokes + 2U;
  if (!BuildIntegratedScale({g_base_nodes, g_historical_strokes,
                             0x43414e5641533033ULL},
                            g_document.get(), g_scene.get(),
                            g_ink_geometry.get(), g_tile_cache.get(),
                            g_scheduler.get(), &scale,
                            &error)) {
    g_result = "{\"error\":\"integrated scale failed: " + error + "\"}";
    return false;
  }
  IntegratedActionReport actions;
  if (!RunIntegratedActionCycle(g_base_nodes, g_historical_strokes,
                                g_document.get(),
                                g_scene.get(), g_ink_geometry.get(),
                                g_tile_cache.get(),
                                g_scheduler.get(), &actions, &error)) {
    g_result = "{\"error\":\"integrated action cycle failed: " + error +
               "\"}";
    return false;
  }
  g_ink_controller = std::make_unique<IntegratedInkController>(
      *g_ink_geometry, *g_document, *g_scene, *g_tile_cache, *g_scheduler);
  SceneCompiler compiler;
  g_oracle = std::make_unique<RuntimeScene>(compiler.CompileFull(*g_document));
  if (g_scene->Digest() != g_oracle->Digest()) {
    g_result = "{\"error\":\"incremental/full scene digest differs\"}";
    return false;
  }
  return true;
}

canvas::poc02::PointerSampleBatch MakeWebInkBatch(
    uint64_t pointer_id, const float* packed, const uint32_t* timestamps_us,
    size_t count, bool first, bool final, float dpr) {
  using namespace canvas::poc02;
  PointerSampleBatch batch{
      .view_id = 1U,
      .viewport_revision = g_view_revision,
      .view_to_world = {.m00 = 1.0F / (g_zoom * dpr),
                        .m11 = 1.0F / (g_zoom * dpr),
                        .tx = g_pan_x,
                        .ty = g_pan_y},
      .device = {.device_id = pointer_id,
                 .tool = PointerTool::kPen,
                 .capabilities = kCapabilityPressure},
  };
  batch.samples.reserve(count);
  for (size_t index = 0U; index < count; ++index) {
    PointerPhase phase = PointerPhase::kMove;
    if (first && index == 0U) phase = PointerPhase::kDown;
    if (final && index + 1U == count) phase = PointerPhase::kUp;
    batch.samples.push_back(PointerSample{
        .pointer_id = pointer_id,
        .sample_sequence = g_live_sample_sequence++,
        .position = {packed[index * 3U], packed[index * 3U + 1U]},
        .pressure = std::clamp(packed[index * 3U + 2U], 0.0F, 1.0F),
        .timestamp_us = timestamps_us[index],
        .phase = phase,
    });
  }
  return batch;
}

int InkStatus(canvas::poc02::Status status) {
  return static_cast<int>(status);
}

canvas::poc03::ViewState InteractiveView(float dpr = 1.0F) {
  using namespace canvas::poc03;
  return ViewState{1U, g_view_revision, 1U,
                   Bounds{g_pan_x, g_pan_y,
                          g_pan_x + 1280.0F / (g_zoom * dpr),
                          g_pan_y + 720.0F / (g_zoom * dpr)},
                   g_zoom, dpr, 1280U, 720U};
}

bool RenderInteractiveInk() {
  using namespace canvas::poc03;
  if (!g_surface || !g_context || !g_scene || !g_ink_geometry) return false;
  const ViewState view = InteractiveView();
  const ViewQueryResult query = QueryView(*g_scene, view, std::nullopt);
  const auto* preview = g_ink_controller
                            ? g_ink_controller->DrawablePreview(
                                  g_scene->source_revision())
                            : nullptr;
  DrawLargeScene(*g_surface->getCanvas(), *g_scene, view,
                 BuildFrame(*g_scene, query, {}), g_ink_geometry.get(),
                 preview);
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kNo);
  return true;
}

bool RenderInteractiveFrame() {
  if (!g_surface || !g_context || !g_scene) return false;
  const auto view = InteractiveView();
  const auto query = canvas::poc03::QueryView(*g_scene, view, std::nullopt);
  DrawLargeScene(*g_surface->getCanvas(), *g_scene, view,
                 BuildFrame(*g_scene, query, {}), g_ink_geometry.get(),
                 g_ink_controller ? g_ink_controller->DrawablePreview(
                     g_scene->source_revision()) : nullptr);
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kNo);
  return true;
}

bool ApplyInteractiveGesture(float previous_x, float previous_y, float current_x,
                             float current_y, float scale, float dpr) {
  canvas::poc03::ViewportTransform transform{g_pan_x, g_pan_y, g_zoom};
  std::string error;
  if (!canvas::poc03::ApplyViewportGesture(
          {previous_x, previous_y, current_x, current_y, scale}, dpr, 0.25F,
          4.0F, {0.0F, 0.0F, 30000.0F, 3000.0F}, &transform, &error)) {
    g_result = std::string("{\"error\":\"") + error + "\"}";
    return false;
  }
  g_pan_x = transform.pan_x;
  g_pan_y = transform.pan_y;
  g_zoom = transform.zoom;
  ++g_view_revision;
  if (g_ink_controller) {
    g_ink_controller->ViewChanged(++g_target_generation);
  }
  return RenderInteractiveFrame();
}

bool ApplyInteractiveDrag(float screen_x, float screen_y) {
  using namespace canvas::poc03;
  if (g_selected_node_id == 0U || !g_document || !g_scene) return true;
  const float world_x = g_pan_x + screen_x / g_zoom;
  const float world_y = g_pan_y + screen_y / g_zoom;
  const auto* current = g_document->Find(g_selected_node_id);
  if (!current) return false;
  NodeRecord moved = *current;
  const float width = g_drag_original_bounds.right - g_drag_original_bounds.left;
  const float height = g_drag_original_bounds.bottom - g_drag_original_bounds.top;
  moved.bounds = {world_x - g_drag_offset_x, world_y - g_drag_offset_y,
                  world_x - g_drag_offset_x + width,
                  world_y - g_drag_offset_y + height};
  ChangeSet changes;
  CompileDiagnostics diagnostics;
  std::string error;
  SceneCompiler compiler;
  if (!g_document->Apply({OperationKind::kUpdate, moved.id, moved}, &changes,
                         &error) ||
      !compiler.ApplyIncremental(*g_document, changes, g_scene.get(),
                                 &diagnostics, &error)) {
    g_result = std::string("{\"error\":\"") + error + "\"}";
    return false;
  }
  g_tile_cache->InvalidateWorld(1U, diagnostics.authoritative_world_dirty,
                                256.0F);
  return RenderInteractiveFrame();
}

bool RenderFrame(uint32_t frame) {
  using namespace canvas::poc03;
  if (!g_surface || !g_context || !g_scene) {
    g_result = "{\"error\":\"physical Web probe is not prepared\"}";
    return false;
  }
  const ViewState view = TraceView(frame);
  const ViewQueryResult query = QueryView(*g_scene, view, std::nullopt);
  g_maximum_candidates =
      std::max(g_maximum_candidates, query.candidates.size());
  g_maximum_visible = std::max(g_maximum_visible, query.visible.size());
  FrameGraph frame_graph = BuildFrame(*g_scene, query, {});
  const std::string visual_digest = frame_graph.VisualDigest();
  OptimizeFrameGraph(&frame_graph);
  if (frame_graph.VisualDigest() != visual_digest) {
    g_result = "{\"error\":\"frame graph optimization changed visual digest\"}";
    return false;
  }
  DrawLargeScene(*g_surface->getCanvas(), *g_scene, view, frame_graph,
                 g_ink_geometry.get());
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kNo);
  return true;
}

const char *Finish() {
  using namespace canvas::poc03;
  if (!g_surface || !g_context || !g_document || !g_scene || !g_oracle) {
    g_result = "{\"error\":\"physical Web probe is not prepared\"}";
    return g_result.c_str();
  }
  const ViewState view = TraceView(599U);
  const ViewQueryResult query = QueryView(*g_scene, view, std::nullopt);
  DrawLargeScene(*g_surface->getCanvas(), *g_scene, view,
                 BuildFrame(*g_scene, query, {}), g_ink_geometry.get());
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
  std::vector<uint8_t> rgba;
  if (!ReadRgba(*g_surface, 1280U, 720U, &rgba)) {
    g_result = "{\"error\":\"WebGL2 readback failed\"}";
    return g_result.c_str();
  }
  const std::string pixel_digest = PixelDigest(rgba);
  const ViewQueryResult oracle_query = QueryView(*g_oracle, view, std::nullopt);
  DrawLargeScene(*g_surface->getCanvas(), *g_oracle, view,
                 BuildFrame(*g_oracle, oracle_query, {}),
                 g_ink_geometry.get());
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
  if (!ReadRgba(*g_surface, 1280U, 720U, &rgba)) {
    g_result = "{\"error\":\"WebGL2 oracle readback failed\"}";
    return g_result.c_str();
  }
  const bool visual_equivalent = pixel_digest == PixelDigest(rgba);
  g_result =
      "{\"backend\":\"ganesh-webgl2\",\"nodes\":" +
      std::to_string(g_base_nodes) + ",\"historical_strokes\":" +
      std::to_string(g_historical_strokes) +
      ",\"ink_document_digest\":\"" +
      g_ink_geometry->document().Digest() + "\",\"document_digest\":\"" +
      g_document->Digest() + "\",\"scene_digest\":\"" + g_scene->Digest() +
      "\",\"oracle_scene_digest\":\"" + g_oracle->Digest() +
      "\",\"pixel_digest\":\"" + pixel_digest +
      "\",\"visual_equivalent\":" + (visual_equivalent ? "true" : "false") +
      ",\"maximum_candidates\":" + std::to_string(g_maximum_candidates) +
      ",\"maximum_visible\":" + std::to_string(g_maximum_visible) +
      ",\"document_bytes\":" + std::to_string(g_document->EstimatedBytes()) +
      ",\"scene_bytes\":" + std::to_string(g_scene->EstimatedBytes()) +
      ",\"source_asset_bytes\":0" + ",\"wasm_linear_memory_bytes\":" +
      std::to_string(static_cast<uint64_t>(emscripten_get_heap_size())) +
      ",\"process_peak_mib\":" +
      std::to_string(static_cast<double>(emscripten_get_heap_size()) /
                     (1024.0 * 1024.0)) +
      ",\"full_incremental_equivalent\":" +
      (g_scene->Digest() == g_oracle->Digest() ? "true" : "false") + "}";
  return g_result.c_str();
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void canvas_poc03_web_reset() { Reset(); }

EMSCRIPTEN_KEEPALIVE const char *canvas_poc03_web_prepare() {
  try {
    Reset();
    if (!Prepare())
      return g_result.c_str();
    g_result = "{\"prepared\":true}";
    return g_result.c_str();
  } catch (const std::exception &error) {
    g_result = std::string("{\"error\":\"") + error.what() + "\"}";
    return g_result.c_str();
  }
}

EMSCRIPTEN_KEEPALIVE const char *canvas_poc03_web_prepare_scale(
    uint32_t base_nodes) {
  try {
    Reset();
    if (!Prepare(base_nodes)) return g_result.c_str();
    g_result = "{\"prepared\":true,\"base_nodes\":" +
               std::to_string(g_base_nodes) +
               ",\"historical_strokes\":" +
               std::to_string(g_historical_strokes) + "}";
    return g_result.c_str();
  } catch (const std::exception& error) {
    g_result = std::string("{\"error\":\"") + error.what() + "\"}";
    return g_result.c_str();
  }
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_render_frame(uint32_t frame) {
  try {
    return RenderFrame(frame) ? 0 : 1;
  } catch (const std::exception &error) {
    g_result = std::string("{\"error\":\"") + error.what() + "\"}";
    return 1;
  }
}

EMSCRIPTEN_KEEPALIVE const char *canvas_poc03_web_finish() {
  try {
    return Finish();
  } catch (const std::exception &error) {
    g_result = std::string("{\"error\":\"") + error.what() + "\"}";
    return g_result.c_str();
  }
}

EMSCRIPTEN_KEEPALIVE const char *canvas_poc03_web_run(const char *selector) {
  try {
    (void)selector;
    Reset();
    if (!Prepare() || !RenderFrame(599U))
      return g_result.c_str();
    return Finish();
  } catch (const std::exception &error) {
    g_result = std::string("{\"error\":\"") + error.what() + "\"}";
    return g_result.c_str();
  }
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_ink_begin(
    uint32_t brush_type, uint32_t pointer_id, const float* packed,
    const uint32_t* timestamps_us, size_t count, float dpr) {
  if (!g_ink_controller || pointer_id == 0U || packed == nullptr ||
      timestamps_us == nullptr || count == 0U || dpr <= 0.0F) {
    return InkStatus(canvas::poc02::Status::kInvalidArgument);
  }
  g_live_sample_sequence = 0U;
  auto batch = MakeWebInkBatch(pointer_id, packed, timestamps_us, count, true,
                               false, dpr);
  const auto status = g_ink_controller->Begin(
      ++g_live_stroke_id, pointer_id,
      canvas::poc03::DeterministicBrush(
          brush_type == 2U ? canvas::poc02::BrushType::kDab
                           : canvas::poc02::BrushType::kVector),
      batch);
  if (status == canvas::poc02::Status::kOk && !RenderInteractiveInk()) {
    return InkStatus(canvas::poc02::Status::kInvalidState);
  }
  return InkStatus(status);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_ink_push(
    uint32_t pointer_id, const float* packed, const uint32_t* timestamps_us,
    size_t count, uint32_t final, float dpr) {
  if (!g_ink_controller || pointer_id == 0U || packed == nullptr ||
      timestamps_us == nullptr || count == 0U || dpr <= 0.0F) {
    return InkStatus(canvas::poc02::Status::kInvalidArgument);
  }
  auto batch = MakeWebInkBatch(pointer_id, packed, timestamps_us, count, false,
                               final != 0U, dpr);
  const uint64_t now_us = batch.samples.back().timestamp_us;
  const auto status = g_ink_controller->Push(std::move(batch), now_us);
  if (status != canvas::poc02::Status::kOk) return InkStatus(status);
  if (final == 0U) {
    return RenderInteractiveInk()
               ? 0
               : InkStatus(canvas::poc02::Status::kInvalidState);
  }
  canvas::poc03::InkCommitDiagnostics diagnostics;
  std::string error;
  if (!g_ink_controller->Commit(g_live_order++, &diagnostics, &error) ||
      !RenderInteractiveInk()) {
    g_result = error;
    return InkStatus(canvas::poc02::Status::kInvalidState);
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_ink_visible() {
  if (!g_ink_controller) {
    return InkStatus(canvas::poc02::Status::kInvalidState);
  }
  const auto frame = g_ink_controller->BeginFrame();
  canvas::poc03::InkCommitDiagnostics diagnostics;
  std::string error;
  if (!frame || !g_ink_controller->CompletePresentation(
                    *frame, g_scene->source_revision(), true, &diagnostics,
                    &error)) {
    g_result = error;
    return InkStatus(canvas::poc02::Status::kInvalidState);
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_ink_cancel() {
  if (!g_ink_controller) {
    return InkStatus(canvas::poc02::Status::kInvalidState);
  }
  const auto status = g_ink_controller->Cancel();
  if (status == canvas::poc02::Status::kOk) RenderInteractiveInk();
  return InkStatus(status);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_transform(
    float previous_x, float previous_y, float current_x, float current_y,
    float scale, float dpr) {
  return ApplyInteractiveGesture(previous_x, previous_y, current_x, current_y,
                                 scale, dpr) ? 0 : 1;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_select_begin(float screen_x,
                                                        float screen_y) {
  if (!g_scene || !g_document) return 1;
  const auto view = InteractiveView();
  const float world_x = g_pan_x + screen_x / g_zoom;
  const float world_y = g_pan_y + screen_y / g_zoom;
  const auto hits = canvas::poc03::HitTest(*g_scene, view, world_x, world_y,
                                            12.0F / g_zoom);
  std::optional<uint64_t> selected;
  for (const uint64_t id : hits) {
    const auto* candidate = g_document->Find(id);
    if (candidate && !candidate->locked &&
        candidate->type != canvas::poc03::NodeType::kStroke) {
      selected = id;
      break;
    }
  }
  if (!selected) {
    g_selected_node_id = 0U;
    return 0;
  }
  const auto* node = g_document->Find(*selected);
  if (!node) return 1;
  g_selected_node_id = *selected;
  g_drag_original_bounds = node->bounds;
  g_drag_offset_x = world_x - node->bounds.left;
  g_drag_offset_y = world_y - node->bounds.top;
  return 0;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_select_move(float screen_x,
                                                       float screen_y) {
  return ApplyInteractiveDrag(screen_x, screen_y) ? 0 : 1;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc03_web_select_end() {
  g_selected_node_id = 0U;
  return 0;
}

} // extern "C"
