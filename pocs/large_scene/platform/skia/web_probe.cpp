#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

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
  g_result.clear();
}

bool EnsureSurface(const char* selector) {
  if (g_surface) {
    return true;
  }
  if (selector == nullptr ||
      emscripten_set_canvas_element_size(selector, 1280, 720) !=
          EMSCRIPTEN_RESULT_SUCCESS) {
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
  if (g_webgl <= 0 || emscripten_webgl_make_context_current(g_webgl) !=
                          EMSCRIPTEN_RESULT_SUCCESS) {
    return false;
  }
  g_context = GrDirectContexts::MakeGL(GrGLMakeNativeInterface());
  if (!g_context) {
    return false;
  }
  GLint framebuffer_id = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_id);
  GrGLFramebufferInfo framebuffer{};
  framebuffer.fFBOID = static_cast<GrGLuint>(framebuffer_id);
  framebuffer.fFormat = static_cast<GrGLenum>(GL_RGBA8);
  const GrBackendRenderTarget target = GrBackendRenderTargets::MakeGL(
      1280, 720, 1, 8, framebuffer);
  g_surface = SkSurfaces::WrapBackendRenderTarget(
      g_context.get(), target, kBottomLeft_GrSurfaceOrigin,
      kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
  return g_surface != nullptr;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void canvas_poc03_web_reset() { Reset(); }

EMSCRIPTEN_KEEPALIVE const char* canvas_poc03_web_run(const char* selector) {
  using namespace canvas::poc03;
  try {
    if (!EnsureSurface(selector)) {
      g_result = "{\"error\":\"WebGL2/Skia surface creation failed\"}";
      return g_result.c_str();
    }
    Document document = GenerateDocument(
        {100000U, 0x43414e5641533033ULL, 1000U, 32.0F});
    SceneCompiler compiler;
    RuntimeScene scene = compiler.CompileFull(document);
    std::string error;
    for (uint32_t update = 0; update < 1000U; ++update) {
      const uint64_t id = 1U + (static_cast<uint64_t>(update) * 7919U) %
                                  100000U;
      NodeRecord changed = *document.Find(id);
      changed.rgba ^= 0x00010101U;
      ++changed.content_revision;
      ChangeSet changes;
      CompileDiagnostics diagnostics;
      if (!document.Apply({OperationKind::kUpdate, id, changed}, &changes,
                          &error) ||
          !compiler.ApplyIncremental(document, changes, &scene, &diagnostics,
                                     &error)) {
        g_result = "{\"error\":\"incremental compile failed\"}";
        return g_result.c_str();
      }
    }
    const RuntimeScene oracle = compiler.CompileFull(document);
    const ViewState view{1U, 1U, 1U,
        Bounds{0.0F, 0.0F, 1280.0F, 720.0F},
        1.0F, 1.0F, 1280U, 720U};
    const ViewQueryResult query = QueryView(scene, view, std::nullopt);
    const FrameGraph frame = BuildFrame(scene, query, {});
    DrawLargeScene(*g_surface->getCanvas(), scene, view, frame);
    g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
    std::vector<uint8_t> rgba;
    if (!ReadRgba(*g_surface, 1280U, 720U, &rgba)) {
      g_result = "{\"error\":\"WebGL2 readback failed\"}";
      return g_result.c_str();
    }
    const std::string pixel_digest = PixelDigest(rgba);
    const ViewQueryResult oracle_query = QueryView(oracle, view, std::nullopt);
    DrawLargeScene(*g_surface->getCanvas(), oracle, view,
                   BuildFrame(oracle, oracle_query, {}));
    g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
    if (!ReadRgba(*g_surface, 1280U, 720U, &rgba)) {
      g_result = "{\"error\":\"WebGL2 oracle readback failed\"}";
      return g_result.c_str();
    }
    const bool visual_equivalent = pixel_digest == PixelDigest(rgba);
    g_result = "{\"backend\":\"ganesh-webgl2\",\"nodes\":100000,\"document_digest\":\"" +
        document.Digest() + "\",\"scene_digest\":\"" + scene.Digest() +
        "\",\"oracle_scene_digest\":\"" + oracle.Digest() +
        "\",\"pixel_digest\":\"" + pixel_digest +
        "\",\"visual_equivalent\":" +
        (visual_equivalent ? "true" : "false") +
        ",\"maximum_candidates\":" + std::to_string(query.candidates.size()) +
        ",\"wasm_linear_memory_bytes\":" +
        std::to_string(static_cast<uint64_t>(emscripten_get_heap_size())) +
        ",\"process_peak_mib\":" +
        std::to_string(static_cast<double>(emscripten_get_heap_size()) /
                       (1024.0 * 1024.0)) +
        ",\"full_incremental_equivalent\":" +
        (scene.Digest() == oracle.Digest() ? "true" : "false") + "}";
    return g_result.c_str();
  } catch (const std::exception& error) {
    g_result = std::string("{\"error\":\"") + error.what() + "\"}";
    return g_result.c_str();
  }
}

}  // extern "C"
