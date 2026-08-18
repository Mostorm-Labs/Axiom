#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "android_gles_adapter.h"
#include "canvas_poc02/ink_engine.h"
#include "ink_skia_renderer.h"

namespace {

canvas::poc02::StrokeDocument g_document;
canvas::poc02::DefaultPreviewSink g_preview;
std::unique_ptr<canvas::poc02::InputRouter> g_router;
std::unique_ptr<canvas::poc02::AndroidGlesAdapter> g_surface;
canvas::poc02::StrokeId g_stroke_id = 0;
uint64_t g_operation_sequence = 1;
uint64_t g_next_sample_sequence = 0;

std::vector<uint8_t> Bytes(JNIEnv* env, jbyteArray source) {
  const jsize size = env->GetArrayLength(source);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  env->GetByteArrayRegion(source, 0, size,
                          reinterpret_cast<jbyte*>(bytes.data()));
  return bytes;
}

jstring Result(JNIEnv* env, canvas::poc02::Status status,
               const std::string& detail = {}) {
  const std::string value = status == canvas::poc02::Status::kOk
      ? std::string("OK")
      : std::string("FAIL ") + std::string(canvas::poc02::StatusName(status)) +
            (detail.empty() ? "" : ": " + detail);
  return env->NewStringUTF(value.c_str());
}

canvas::poc02::PointerSampleBatch Batch(JNIEnv* env, jfloatArray packed_array,
                                        jlongArray timestamps_array,
                                        jint phase, jlong viewport_revision) {
  const jsize packed_size = env->GetArrayLength(packed_array);
  const jsize sample_count = packed_size / 6;
  std::vector<jfloat> packed(static_cast<size_t>(packed_size));
  std::vector<jlong> timestamps(static_cast<size_t>(sample_count));
  env->GetFloatArrayRegion(packed_array, 0, packed_size, packed.data());
  env->GetLongArrayRegion(timestamps_array, 0, sample_count, timestamps.data());
  canvas::poc02::PointerSampleBatch batch{
      .view_id = 1,
      .viewport_revision = static_cast<uint64_t>(viewport_revision),
      .view_to_world = {},
      .device = {.device_id = 1,
                 .tool = canvas::poc02::PointerTool::kPen,
                 .capabilities = canvas::poc02::kCapabilityPressure |
                                 canvas::poc02::kCapabilityTilt |
                                 canvas::poc02::kCapabilityContact},
      .samples = {},
  };
  batch.samples.reserve(static_cast<size_t>(sample_count));
  for (jsize index = 0; index < sample_count; ++index) {
    canvas::poc02::PointerPhase pointer_phase = canvas::poc02::PointerPhase::kMove;
    if (phase == 0 && index == 0) pointer_phase = canvas::poc02::PointerPhase::kDown;
    if (phase == 2 && index + 1 == sample_count)
      pointer_phase = canvas::poc02::PointerPhase::kUp;
    batch.samples.push_back(canvas::poc02::PointerSample{
        .pointer_id = 1,
        .sample_sequence = g_next_sample_sequence++,
        .position = {packed[index * 6], packed[index * 6 + 1]},
        .pressure = packed[index * 6 + 2],
        .tilt = {packed[index * 6 + 3], packed[index * 6 + 4]},
        .contact_size = {packed[index * 6 + 5], packed[index * 6 + 5]},
        .timestamp_us = static_cast<uint64_t>(timestamps[index]),
        .phase = pointer_phase,
    });
  }
  return batch;
}

canvas::poc02::Status Render(std::vector<uint8_t>* pixels = nullptr) {
  if (!g_surface) return canvas::poc02::Status::kInvalidState;
  const auto* preview = g_stroke_id == 0 ? nullptr : g_preview.Find(g_stroke_id);
  return g_surface->Render(g_document, preview, pixels);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc02_CanvasInkView_nativeAttach(
    JNIEnv* env, jobject, jobject surface, jint width, jint height) {
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) return Result(env, canvas::poc02::Status::kInvalidArgument);
  g_surface = std::make_unique<canvas::poc02::AndroidGlesAdapter>();
  const auto status = g_surface->Attach(window, static_cast<uint32_t>(width),
                                        static_cast<uint32_t>(height));
  ANativeWindow_release(window);
  if (status == canvas::poc02::Status::kOk) Render();
  return Result(env, status, g_surface->error());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc02_CanvasInkView_nativePointerBatch(
    JNIEnv* env, jobject, jint phase, jfloatArray packed,
    jlongArray timestamps, jlong viewport_revision, jint brush_type) {
  if (packed == nullptr || timestamps == nullptr ||
      env->GetArrayLength(packed) == 0 || env->GetArrayLength(packed) % 6 != 0 ||
      env->GetArrayLength(timestamps) * 6 != env->GetArrayLength(packed)) {
    return Result(env, canvas::poc02::Status::kInvalidArgument);
  }
  canvas::poc02::Status status = canvas::poc02::Status::kOk;
  if (phase == 0) {
    g_stroke_id = static_cast<uint64_t>(timestamps == nullptr ? 1 :
        std::max<jlong>(1, env->GetArrayLength(timestamps)) + g_operation_sequence * 1000);
    g_next_sample_sequence = 0;
    if (!g_router) g_router = std::make_unique<canvas::poc02::InputRouter>(
        g_document, g_preview);
    canvas::poc02::BrushDescriptor brush{
        .type = brush_type == 2 ? canvas::poc02::BrushType::kDab
                                : canvas::poc02::BrushType::kVector,
        .size = brush_type == 2 ? 16.0F : 8.0F,
        .spacing = 0.35F,
        .opacity = 0.9F,
        .jitter = brush_type == 2 ? 0.12F : 0.0F,
        .resource_id = {},
        .resource_content_hash = {},
    };
    auto batch = Batch(env, packed, timestamps, phase, viewport_revision);
    status = g_router->Begin(g_stroke_id, 1, brush, batch);
  } else {
    if (!g_router) return Result(env, canvas::poc02::Status::kInvalidState);
    auto batch = Batch(env, packed, timestamps, phase, viewport_revision);
    const uint64_t now = batch.samples.back().timestamp_us;
    status = g_router->Submit(std::move(batch), now);
    if (status == canvas::poc02::Status::kOk) status = g_router->Drain(now);
    if (status == canvas::poc02::Status::kOk && phase == 2) {
      canvas::poc02::AddStrokeOperation operation;
      status = g_router->End(g_operation_sequence++, &operation);
    }
  }
  if (status == canvas::poc02::Status::kOk) {
    status = Render();
    if (status == canvas::poc02::Status::kOk && phase == 2) {
      status = g_router->AcknowledgeCanonicalVisible(g_stroke_id,
                                                     g_document.revision());
    }
  }
  return Result(env, status, g_surface ? g_surface->error() : std::string());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc02_CanvasInkView_nativeReplayAcceptance(
    JNIEnv* env, jobject, jbyteArray replay_array, jbyteArray golden_array,
    jbyteArray dab_replay_array, jbyteArray dab_golden_array,
    jstring output_path) {
  const auto replay = Bytes(env, replay_array);
  const auto golden = Bytes(env, golden_array);
  const auto dab_replay = Bytes(env, dab_replay_array);
  const auto dab_golden = Bytes(env, dab_golden_array);
  std::string error;
  struct AcceptanceResult {
    std::string document_digest;
    std::string stroke_digest;
    std::string preview_digest;
    std::vector<uint8_t> pixels;
    canvas::poc02::PixelMetrics metrics;
    double ratio = 0.0;
  };
  const auto run = [&](const std::vector<uint8_t>& fixture_bytes,
                       const std::vector<uint8_t>& golden_bytes,
                       AcceptanceResult* result) {
    canvas::poc02::ReplayFixture fixture;
    canvas::poc02::Status status = canvas::poc02::ParseReplayFixture(
        std::string_view(reinterpret_cast<const char*>(fixture_bytes.data()),
                         fixture_bytes.size()),
        &fixture, &error);
    g_document = {};
    g_preview = {};
    canvas::poc02::AddStrokeOperation operation;
    if (status == canvas::poc02::Status::kOk) {
      status = canvas::poc02::RunReplayFixture(
          fixture, &g_document, &g_preview, &operation, &error);
    }
    if (status == canvas::poc02::Status::kOk) {
      status = Render(&result->pixels);
    }
    if (status == canvas::poc02::Status::kOk) {
      result->metrics = canvas::poc02::CompareRgba(
          golden_bytes, result->pixels, 2);
      result->ratio = result->metrics.total_pixels == 0
          ? 0.0
          : static_cast<double>(result->metrics.matching_pixels) /
                result->metrics.total_pixels;
      if (result->ratio < 0.999 || result->metrics.maximum_channel_delta > 2) {
        return canvas::poc02::Status::kInvalidState;
      }
      result->document_digest = g_document.Digest();
      result->stroke_digest = canvas::poc02::StrokeDigest(operation.stroke);
      result->preview_digest = g_preview.ModelDigest();
      g_operation_sequence = operation.sequence + 1;
    }
    return status;
  };
  g_router.reset();
  g_stroke_id = 0;
  AcceptanceResult vector_result;
  AcceptanceResult dab_result;
  auto status = run(replay, golden, &vector_result);
  if (status == canvas::poc02::Status::kOk) {
    status = run(dab_replay, dab_golden, &dab_result);
  }
  if (status != canvas::poc02::Status::kOk) return Result(env, status, error);
  const char* raw_path = env->GetStringUTFChars(output_path, nullptr);
  std::ofstream output(raw_path, std::ios::binary | std::ios::trunc);
  env->ReleaseStringUTFChars(output_path, raw_path);
  output.write(reinterpret_cast<const char*>(vector_result.pixels.data()),
               static_cast<std::streamsize>(vector_result.pixels.size()));
  std::ostringstream json;
  json << "{\"platform\":\"android\",\"backend\":\"ganesh-gles3\""
       << ",\"document_digest\":\"" << vector_result.document_digest
       << "\",\"stroke_digest\":\"" << vector_result.stroke_digest
       << "\",\"preview_digest\":\"" << vector_result.preview_digest
       << "\",\"numeric_digest\":\""
       << canvas::poc02::NumericConformanceDigest()
       << "\",\"matching_ratio\":" << vector_result.ratio
       << ",\"maximum_channel_delta\":"
       << static_cast<uint32_t>(vector_result.metrics.maximum_channel_delta)
       << ",\"dab_document_digest\":\"" << dab_result.document_digest
       << "\",\"dab_stroke_digest\":\"" << dab_result.stroke_digest
       << "\",\"dab_preview_digest\":\"" << dab_result.preview_digest
       << "\",\"dab_matching_ratio\":" << dab_result.ratio
       << ",\"dab_maximum_channel_delta\":"
       << static_cast<uint32_t>(dab_result.metrics.maximum_channel_delta)
       << "}";
  __android_log_print(ANDROID_LOG_INFO, "CanvasPOC02", "CANVAS_POC02_RESULT %s",
                      json.str().c_str());
  return env->NewStringUTF(json.str().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_mostorm_canvas_poc02_CanvasInkView_nativeCancel(JNIEnv* env,
                                                          jobject) {
  if (!g_router) return Result(env, canvas::poc02::Status::kInvalidState);
  const canvas::poc02::Status status = g_router->Cancel();
  if (status == canvas::poc02::Status::kOk) {
    g_stroke_id = 0;
    const canvas::poc02::Status render_status = Render();
    if (render_status != canvas::poc02::Status::kOk) {
      return Result(env, render_status, g_surface ? g_surface->error()
                                                  : std::string());
    }
  }
  return Result(env, status);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_mostorm_canvas_poc02_CanvasInkView_nativeDetach(JNIEnv*, jobject) {
  if (g_surface) g_surface->Detach();
  g_surface.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_mostorm_canvas_poc02_CanvasInkView_nativeDestroy(JNIEnv*, jobject) {
  g_router.reset();
  g_surface.reset();
  g_document = {};
  g_preview = {};
}
