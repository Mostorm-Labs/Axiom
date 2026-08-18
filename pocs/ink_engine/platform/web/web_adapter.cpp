#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "canvas_poc02/ink_engine.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "ink_skia_renderer.h"

namespace {

constexpr uint32_t kWidth = 800;
constexpr uint32_t kHeight = 600;

canvas::poc02::StrokeDocument g_document;
canvas::poc02::DefaultPreviewSink g_preview;
std::unique_ptr<canvas::poc02::InputRouter> g_router;
canvas::poc02::InkSkiaRenderer g_renderer;
canvas::poc02::StrokeId g_stroke_id = 0;
uint64_t g_next_sample_sequence = 0;
uint64_t g_next_operation_sequence = 1;
canvas::poc02::StrokeId g_pending_visible_stroke_id = 0;
uint64_t g_pending_visible_revision = 0;
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl = 0;
sk_sp<GrDirectContext> g_context;
sk_sp<SkSurface> g_surface;
std::string g_last_error;

int Code(canvas::poc02::Status status) {
  return static_cast<int>(status);
}

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
}

canvas::poc02::PointerSampleBatch MakeBatch(
    const float* packed, const uint32_t* timestamps_us, size_t count,
    bool first, bool final, float dpr) {
  canvas::poc02::PointerSampleBatch batch{
      .view_id = 1,
      .viewport_revision = 1,
      .view_to_world = {.m00 = 1.0F / dpr, .m11 = 1.0F / dpr},
      .device = {.device_id = 1,
                 .tool = canvas::poc02::PointerTool::kPen,
                 .capabilities = canvas::poc02::kCapabilityPressure},
  };
  batch.samples.reserve(count);
  for (size_t index = 0; index < count; ++index) {
    canvas::poc02::PointerPhase phase = canvas::poc02::PointerPhase::kMove;
    if (first && index == 0) phase = canvas::poc02::PointerPhase::kDown;
    if (final && index + 1 == count) phase = canvas::poc02::PointerPhase::kUp;
    batch.samples.push_back(canvas::poc02::PointerSample{
        .pointer_id = 1,
        .sample_sequence = g_next_sample_sequence++,
        .position = {packed[index * 3], packed[index * 3 + 1]},
        .pressure = packed[index * 3 + 2],
        .timestamp_us = timestamps_us[index],
        .phase = phase,
    });
  }
  return batch;
}

size_t CopyDigest(const std::string& digest, char* output, size_t capacity) {
  const size_t required = digest.size() + 1;
  if (output != nullptr && capacity >= required) {
    std::copy(digest.begin(), digest.end(), output);
    output[digest.size()] = '\0';
  }
  return required;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int canvas_poc02_reset() {
  g_router.reset();
  g_document = {};
  g_preview = {};
  g_stroke_id = 0;
  g_next_sample_sequence = 0;
  g_next_operation_sequence = 1;
  g_pending_visible_stroke_id = 0;
  g_pending_visible_revision = 0;
  DestroySurface();
  return 0;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_replay(const char* ndjson, size_t size,
                                             uint32_t* sample_count) {
  if (ndjson == nullptr || sample_count == nullptr) {
    return Code(canvas::poc02::Status::kInvalidArgument);
  }
  canvas::poc02::ReplayFixture fixture;
  std::string error;
  canvas::poc02::Status status = canvas::poc02::ParseReplayFixture(
      std::string_view(ndjson, size), &fixture, &error);
  if (status != canvas::poc02::Status::kOk) return Code(status);
  g_router.reset();
  g_document = {};
  g_preview = {};
  g_pending_visible_stroke_id = 0;
  g_pending_visible_revision = 0;
  canvas::poc02::AddStrokeOperation operation;
  status = canvas::poc02::RunReplayFixture(fixture, &g_document, &g_preview,
                                           &operation, &error);
  if (status == canvas::poc02::Status::kOk) {
    g_stroke_id = operation.stroke.id;
    g_next_operation_sequence = operation.sequence + 1;
    *sample_count = static_cast<uint32_t>(operation.stroke.confirmed_samples.size());
  }
  return Code(status);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_surface_create(const char* selector) {
  DestroySurface();
  if (selector == nullptr ||
      emscripten_set_canvas_element_size(selector, kWidth, kHeight) !=
          EMSCRIPTEN_RESULT_SUCCESS) {
    return Code(canvas::poc02::Status::kInvalidArgument);
  }
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = true;
  attributes.antialias = true;
  attributes.depth = false;
  attributes.stencil = true;
  attributes.premultipliedAlpha = true;
  attributes.preserveDrawingBuffer = true;
  attributes.majorVersion = 2;
  attributes.enableExtensionsByDefault = false;
  g_webgl = emscripten_webgl_create_context(selector, &attributes);
  if (g_webgl <= 0 || emscripten_webgl_make_context_current(g_webgl) !=
                          EMSCRIPTEN_RESULT_SUCCESS) {
    DestroySurface();
    return Code(canvas::poc02::Status::kInvalidState);
  }
  g_context = GrDirectContexts::MakeGL(GrGLMakeNativeInterface());
  GLint framebuffer_id = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_id);
  GrGLFramebufferInfo framebuffer{static_cast<GrGLuint>(framebuffer_id),
                                  static_cast<GrGLenum>(GL_RGBA8)};
  GrBackendRenderTarget target =
      GrBackendRenderTargets::MakeGL(kWidth, kHeight, 1, 8, framebuffer);
  g_surface = SkSurfaces::WrapBackendRenderTarget(
      g_context.get(), target, kBottomLeft_GrSurfaceOrigin,
      kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
  return g_surface == nullptr ? Code(canvas::poc02::Status::kInvalidState) : 0;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_render() {
  if (g_surface == nullptr || g_context == nullptr) {
    return Code(canvas::poc02::Status::kInvalidState);
  }
  const canvas::poc02::DefaultPreviewSink::State* state =
      g_stroke_id == 0 ? nullptr : g_preview.Find(g_stroke_id);
  g_renderer.Draw(*g_surface->getCanvas(), kWidth, kHeight, g_document, state);
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kNo);
  return 0;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_compare_golden(
    const uint8_t* expected, size_t expected_size, double* matching_ratio,
    uint32_t* maximum_channel_delta) {
  if (expected == nullptr || matching_ratio == nullptr ||
      maximum_channel_delta == nullptr || g_surface == nullptr ||
      g_context == nullptr) {
    return Code(canvas::poc02::Status::kInvalidArgument);
  }
  try {
    g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
    std::vector<uint8_t> actual;
    if (!g_renderer.Readback(*g_surface, kWidth, kHeight, &actual)) {
      return Code(canvas::poc02::Status::kInvalidState);
    }
    const canvas::poc02::PixelMetrics metrics = canvas::poc02::CompareRgba(
        std::span<const uint8_t>(expected, expected_size), actual, 2);
    if (metrics.total_pixels == 0) {
      return Code(canvas::poc02::Status::kInvalidArgument);
    }
    *matching_ratio = static_cast<double>(metrics.matching_pixels) /
                      static_cast<double>(metrics.total_pixels);
    *maximum_channel_delta = metrics.maximum_channel_delta;
    g_last_error.clear();
    return 0;
  } catch (const std::exception& exception) {
    g_last_error = exception.what();
  } catch (...) {
    g_last_error = "unknown C++ exception";
  }
  return Code(canvas::poc02::Status::kInvalidState);
}

EMSCRIPTEN_KEEPALIVE size_t canvas_poc02_last_error(char* output,
                                                    size_t capacity) {
  return CopyDigest(g_last_error, output, capacity);
}

EMSCRIPTEN_KEEPALIVE size_t canvas_poc02_readback(uint8_t* output,
                                                  size_t capacity) {
  constexpr size_t required = static_cast<size_t>(kWidth) * kHeight * 4U;
  if (output == nullptr || capacity < required || g_surface == nullptr ||
      g_context == nullptr) {
    return required;
  }
  g_context->flushAndSubmit(g_surface.get(), GrSyncCpu::kYes);
  std::vector<uint8_t> actual;
  if (!g_renderer.Readback(*g_surface, kWidth, kHeight, &actual)) return 0;
  std::copy(actual.begin(), actual.end(), output);
  return required;
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_begin(
    uint32_t stroke_id, uint32_t brush_type, float brush_size,
    const float* packed, const uint32_t* timestamps_us, size_t count,
    float dpr) {
  if (stroke_id == 0 || packed == nullptr || timestamps_us == nullptr ||
      count == 0 || dpr <= 0.0F) {
    return Code(canvas::poc02::Status::kInvalidArgument);
  }
  if (!g_router) g_router = std::make_unique<canvas::poc02::InputRouter>(
      g_document, g_preview);
  const uint64_t previous_sample_sequence = g_next_sample_sequence;
  g_next_sample_sequence = 0;
  canvas::poc02::BrushDescriptor brush{
      .type = brush_type == 2 ? canvas::poc02::BrushType::kDab
                              : canvas::poc02::BrushType::kVector,
      .size = brush_size,
      .spacing = 0.35F,
      .opacity = 0.9F,
      .jitter = brush_type == 2 ? 0.12F : 0.0F,
  };
  auto batch = MakeBatch(packed, timestamps_us, count, true, false, dpr);
  const canvas::poc02::Status status =
      g_router->Begin(stroke_id, 1, brush, batch);
  if (status == canvas::poc02::Status::kOk) {
    g_stroke_id = stroke_id;
  } else {
    g_next_sample_sequence = previous_sample_sequence;
  }
  return Code(status);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_push_batch(
    const float* packed, const uint32_t* timestamps_us, size_t count,
    uint32_t final, float dpr) {
  if (!g_router || packed == nullptr || timestamps_us == nullptr || count == 0) {
    return Code(canvas::poc02::Status::kInvalidState);
  }
  auto batch = MakeBatch(packed, timestamps_us, count, false, final != 0, dpr);
  const uint64_t now = batch.samples.back().timestamp_us;
  canvas::poc02::Status status = g_router->Submit(std::move(batch), now);
  if (status == canvas::poc02::Status::kOk) status = g_router->Drain(now);
  return Code(status);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_end() {
  if (!g_router) return Code(canvas::poc02::Status::kInvalidState);
  canvas::poc02::AddStrokeOperation operation;
  const canvas::poc02::Status status =
      g_router->End(g_next_operation_sequence++, &operation);
  if (status == canvas::poc02::Status::kOk) {
    g_pending_visible_stroke_id = operation.stroke.id;
    g_pending_visible_revision = g_document.revision();
  }
  return Code(status);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_cancel() {
  return g_router ? Code(g_router->Cancel())
                  : Code(canvas::poc02::Status::kInvalidState);
}

EMSCRIPTEN_KEEPALIVE int canvas_poc02_visible() {
  if (!g_router || g_pending_visible_stroke_id == 0 ||
      g_pending_visible_revision == 0) {
    return Code(canvas::poc02::Status::kInvalidState);
  }
  const auto status = g_router->AcknowledgeCanonicalVisible(
      g_pending_visible_stroke_id, g_pending_visible_revision);
  if (status == canvas::poc02::Status::kOk) {
    g_pending_visible_stroke_id = 0;
    g_pending_visible_revision = 0;
  }
  return Code(status);
}

EMSCRIPTEN_KEEPALIVE size_t canvas_poc02_document_digest(char* output,
                                                         size_t capacity) {
  return CopyDigest(g_document.Digest(), output, capacity);
}

EMSCRIPTEN_KEEPALIVE size_t canvas_poc02_stroke_digest(char* output,
                                                       size_t capacity) {
  const auto* stroke = g_document.Find(g_stroke_id);
  return CopyDigest(stroke == nullptr ? std::string() : StrokeDigest(*stroke),
                    output, capacity);
}

EMSCRIPTEN_KEEPALIVE size_t canvas_poc02_preview_digest(char* output,
                                                        size_t capacity) {
  return CopyDigest(g_preview.ModelDigest(), output, capacity);
}

EMSCRIPTEN_KEEPALIVE size_t canvas_poc02_numeric_digest(char* output,
                                                        size_t capacity) {
  return CopyDigest(canvas::poc02::NumericConformanceDigest(), output, capacity);
}

EMSCRIPTEN_KEEPALIVE uint32_t canvas_poc02_event_count() {
  return static_cast<uint32_t>(g_preview.events().size());
}

EMSCRIPTEN_KEEPALIVE uint32_t canvas_poc02_confirmed_count() {
  const auto* stroke = g_document.Find(g_stroke_id);
  return stroke == nullptr ? 0U
                           : static_cast<uint32_t>(stroke->confirmed_samples.size());
}

}  // extern "C"
