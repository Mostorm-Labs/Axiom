#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "canvas_poc/canvas_poc.h"
#include "foundation.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "platform_bridge_internal.h"
#include "scene_compiler.h"
#include "skia_scene_renderer.h"

namespace {

canvas_poc_handle_t g_runtime = 0;
canvas_poc_handle_t g_document = 0;
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl = 0;
sk_sp<GrDirectContext> g_context;
sk_sp<SkSurface> g_surface;
std::vector<uint8_t> g_readback;

void DestroySurface() {
  g_surface.reset();
  if (g_context != nullptr) {
    g_context->abandonContext();
    g_context.reset();
  }
  if (g_webgl != 0) {
    emscripten_webgl_destroy_context(g_webgl);
    g_webgl = 0;
  }
  g_readback.clear();
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE canvas_poc_status_t canvas_poc_web_reset() {
  DestroySurface();
  if (g_document != 0) {
    canvas_poc_document_destroy(g_document);
    g_document = 0;
  }
  if (g_runtime != 0) {
    canvas_poc_runtime_destroy(g_runtime);
    g_runtime = 0;
  }
  return CANVAS_POC_STATUS_OK;
}

EMSCRIPTEN_KEEPALIVE canvas_poc_status_t canvas_poc_web_load_assets(
    const uint8_t* checker, size_t checker_size, const uint8_t* font,
    size_t font_size) {
  canvas_poc_web_reset();
  canvas_poc_runtime_config_v1 runtime_config{};
  runtime_config.struct_size = sizeof(runtime_config);
  runtime_config.abi_version = CANVAS_POC_ABI_VERSION;
  canvas_poc_status_t status =
      canvas_poc_runtime_create(&runtime_config, &g_runtime);
  if (status != CANVAS_POC_STATUS_OK) return status;
  status = canvas_poc_runtime_register_asset(
      g_runtime, "checker.png", 11, checker, checker_size);
  if (status != CANVAS_POC_STATUS_OK) return status;
  status = canvas_poc_runtime_register_asset(g_runtime, "roboto.ttf", 10, font,
                                             font_size);
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
  return canvas_poc_document_create(g_runtime, &config, &g_document);
}

EMSCRIPTEN_KEEPALIVE canvas_poc_status_t canvas_poc_web_replay(
    const char* ndjson, size_t ndjson_size) {
  return canvas_poc_document_apply_ndjson(g_document, ndjson, ndjson_size);
}

EMSCRIPTEN_KEEPALIVE canvas_poc_status_t canvas_poc_web_digest(
    char* buffer, size_t buffer_size, size_t* required) {
  return canvas_poc_document_digest(g_document, buffer, buffer_size, required);
}

EMSCRIPTEN_KEEPALIVE canvas_poc_status_t canvas_poc_web_surface_create(
    const char* canvas_selector) {
  DestroySurface();
  if (canvas_selector == nullptr ||
      emscripten_set_canvas_element_size(canvas_selector, 800, 600) !=
          EMSCRIPTEN_RESULT_SUCCESS) {
    canvas::poc01::SetLastError("Web canvas selector is invalid");
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
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
  attributes.minorVersion = 0;
  attributes.enableExtensionsByDefault = false;
  attributes.explicitSwapControl = false;
  attributes.renderViaOffscreenBackBuffer = false;
  g_webgl = emscripten_webgl_create_context(canvas_selector, &attributes);
  if (g_webgl <= 0 ||
      emscripten_webgl_make_context_current(g_webgl) !=
          EMSCRIPTEN_RESULT_SUCCESS) {
    canvas::poc01::SetLastError("WebGL2 context creation failed");
    DestroySurface();
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
  }
  g_context = GrDirectContexts::MakeGL(GrGLMakeNativeInterface());
  if (g_context == nullptr) {
    canvas::poc01::SetLastError("Skia failed to create Ganesh WebGL2 context");
    DestroySurface();
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
  }
  GLint framebuffer_id = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_id);
  GrGLFramebufferInfo framebuffer{};
  framebuffer.fFBOID = static_cast<GrGLuint>(framebuffer_id);
  framebuffer.fFormat = static_cast<GrGLenum>(GL_RGBA8);
  GrBackendRenderTarget target =
      GrBackendRenderTargets::MakeGL(800, 600, 1, 8, framebuffer);
  g_surface = SkSurfaces::WrapBackendRenderTarget(
      g_context.get(), target, kBottomLeft_GrSurfaceOrigin,
      kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
  if (g_surface == nullptr) {
    canvas::poc01::SetLastError("Skia failed to wrap WebGL2 framebuffer");
    DestroySurface();
    return CANVAS_POC_STATUS_RENDER_ERROR;
  }
  return CANVAS_POC_STATUS_OK;
}

EMSCRIPTEN_KEEPALIVE canvas_poc_status_t canvas_poc_web_render() {
  std::shared_ptr<canvas::poc01::Document> document =
      canvas::poc01::ResolveDocumentForPlatform(g_document);
  if (document == nullptr || g_surface == nullptr) {
    canvas::poc01::SetLastError("Web document or surface is not ready");
    return CANVAS_POC_STATUS_INVALID_HANDLE;
  }
  const canvas::poc01::RuntimeScene scene =
      canvas::poc01::SceneCompiler().Compile(*document);
  canvas::poc01::SkiaSceneRenderer renderer;
  canvas_poc_status_t status =
      renderer.Draw(*g_surface->getCanvas(), scene, document->assets());
  if (status != CANVAS_POC_STATUS_OK) return status;
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
  status = renderer.Readback(*g_surface, 800, 600, &g_readback);
  return status;
}

EMSCRIPTEN_KEEPALIVE canvas_poc_status_t canvas_poc_web_compare_golden(
    const uint8_t* expected, size_t expected_size, double* matching_ratio,
    uint32_t* maximum_channel_delta) {
  if (expected == nullptr || matching_ratio == nullptr ||
      maximum_channel_delta == nullptr) {
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  const canvas::poc01::VisualMetrics metrics = canvas::poc01::CompareRgba(
      std::span<const uint8_t>(expected, expected_size), g_readback, 2);
  if (metrics.total_pixels == 0) {
    canvas::poc01::SetLastError("golden and WebGL readback sizes differ");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  *matching_ratio = static_cast<double>(metrics.matching_pixels) /
                    static_cast<double>(metrics.total_pixels);
  *maximum_channel_delta = metrics.maximum_channel_delta;
  return CANVAS_POC_STATUS_OK;
}

EMSCRIPTEN_KEEPALIVE canvas_poc_status_t canvas_poc_web_readback(
    uint8_t* buffer, size_t buffer_size, size_t* required) {
  return canvas::poc01::CopyToCaller(g_readback, buffer, buffer_size, required);
}

EMSCRIPTEN_KEEPALIVE const char* canvas_poc_web_backend() {
  return "ganesh-webgl2";
}

}  // extern "C"
