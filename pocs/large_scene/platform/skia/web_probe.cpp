#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "skia_large_scene_renderer.h"

namespace {

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl = 0;
sk_sp<GrDirectContext> g_context;
sk_sp<SkSurface> g_surface;
std::string g_result;
std::unique_ptr<canvas::poc03::Document> g_document;
std::unique_ptr<canvas::poc03::RuntimeScene> g_scene;
std::unique_ptr<canvas::poc03::RuntimeScene> g_oracle;
size_t g_maximum_candidates = 0U;
size_t g_maximum_visible = 0U;

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
  g_oracle.reset();
  g_scene.reset();
  g_document.reset();
  g_maximum_candidates = 0U;
  g_maximum_visible = 0U;
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

bool Prepare() {
  using namespace canvas::poc03;
  if (!EnsureSurface("#scene")) {
    return false;
  }
  g_document = std::make_unique<Document>(GenerateDocument(
      GeneratorConfig{100000U, 0x43414e5641533033ULL, 1000U, 32.0F}));
  SceneCompiler compiler;
  g_scene = std::make_unique<RuntimeScene>(compiler.CompileFull(*g_document));
  std::string error;
  for (uint32_t update = 0U; update < 1000U; ++update) {
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
      g_result = "{\"error\":\"incremental compile failed\"}";
      return false;
    }
  }
  g_oracle = std::make_unique<RuntimeScene>(compiler.CompileFull(*g_document));
  if (g_scene->Digest() != g_oracle->Digest()) {
    g_result = "{\"error\":\"incremental/full scene digest differs\"}";
    return false;
  }
  return true;
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
  DrawLargeScene(*g_surface->getCanvas(), *g_scene, view, frame_graph);
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
                 BuildFrame(*g_scene, query, {}));
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
  std::vector<uint8_t> rgba;
  if (!ReadRgba(*g_surface, 1280U, 720U, &rgba)) {
    g_result = "{\"error\":\"WebGL2 readback failed\"}";
    return g_result.c_str();
  }
  const std::string pixel_digest = PixelDigest(rgba);
  const ViewQueryResult oracle_query = QueryView(*g_oracle, view, std::nullopt);
  DrawLargeScene(*g_surface->getCanvas(), *g_oracle, view,
                 BuildFrame(*g_oracle, oracle_query, {}));
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
  if (!ReadRgba(*g_surface, 1280U, 720U, &rgba)) {
    g_result = "{\"error\":\"WebGL2 oracle readback failed\"}";
    return g_result.c_str();
  }
  const bool visual_equivalent = pixel_digest == PixelDigest(rgba);
  g_result =
      "{\"backend\":\"ganesh-webgl2\",\"nodes\":100000,\"document_digest\":\"" +
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

} // extern "C"
