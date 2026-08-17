#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include "canvas_poc/canvas_poc.h"

namespace {

canvas_poc_runtime_config_v1 RuntimeConfig() {
  canvas_poc_runtime_config_v1 config{};
  config.struct_size = sizeof(config);
  config.abi_version = CANVAS_POC_ABI_VERSION;
  return config;
}

canvas_poc_document_config_v1 DocumentConfig() {
  canvas_poc_document_config_v1 config{};
  config.struct_size = sizeof(config);
  config.abi_version = CANVAS_POC_ABI_VERSION;
  config.page_width = 800;
  config.page_height = 600;
  config.background_rgba[0] = 244;
  config.background_rgba[1] = 245;
  config.background_rgba[2] = 247;
  config.background_rgba[3] = 255;
  return config;
}

TEST(CApiTest, GenerationHandleRejectsUseAfterDestroy) {
  canvas_poc_handle_t first = 0;
  auto config = RuntimeConfig();
  ASSERT_EQ(canvas_poc_runtime_create(&config, &first), CANVAS_POC_STATUS_OK);
  ASSERT_EQ(canvas_poc_runtime_destroy(first), CANVAS_POC_STATUS_OK);
  EXPECT_EQ(canvas_poc_runtime_destroy(first),
            CANVAS_POC_STATUS_INVALID_HANDLE);

  canvas_poc_handle_t second = 0;
  ASSERT_EQ(canvas_poc_runtime_create(&config, &second), CANVAS_POC_STATUS_OK);
  EXPECT_NE(first, second);
  EXPECT_EQ(canvas_poc_runtime_destroy(first),
            CANVAS_POC_STATUS_INVALID_HANDLE);
  EXPECT_EQ(canvas_poc_runtime_destroy(second), CANVAS_POC_STATUS_OK);
}

TEST(CApiTest, StructSizeAndVersionAreValidated) {
  canvas_poc_handle_t runtime = 0;
  auto config = RuntimeConfig();
  config.struct_size = sizeof(config) - 1;
  EXPECT_EQ(canvas_poc_runtime_create(&config, &runtime),
            CANVAS_POC_STATUS_ABI_MISMATCH);
  config = RuntimeConfig();
  config.abi_version = 99;
  EXPECT_EQ(canvas_poc_runtime_create(&config, &runtime),
            CANVAS_POC_STATUS_ABI_MISMATCH);
}

TEST(CApiTest, DetailedErrorUsesCallerProvidedBuffer) {
  EXPECT_EQ(canvas_poc_runtime_destroy(0), CANVAS_POC_STATUS_INVALID_HANDLE);
  size_t required = 0;
  EXPECT_EQ(canvas_poc_last_error(nullptr, 0, &required),
            CANVAS_POC_STATUS_BUFFER_TOO_SMALL);
  ASSERT_GT(required, 1U);
  std::vector<char> message(required);
  EXPECT_EQ(canvas_poc_last_error(message.data(), message.size(), &required),
            CANVAS_POC_STATUS_OK);
  EXPECT_NE(std::string(message.data()).find("handle"), std::string::npos);
}

TEST(CApiTest, BatchFailureDoesNotPartiallyMutateDocument) {
  canvas_poc_handle_t runtime = 0;
  canvas_poc_handle_t document = 0;
  auto runtime_config = RuntimeConfig();
  auto document_config = DocumentConfig();
  ASSERT_EQ(canvas_poc_runtime_create(&runtime_config, &runtime),
            CANVAS_POC_STATUS_OK);
  ASSERT_EQ(canvas_poc_document_create(runtime, &document_config, &document),
            CANVAS_POC_STATUS_OK);
  const std::string replay =
      "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":1,\"type\":\"rect\",\"order\":1,\"x\":0,\"y\":0,\"width\":10,\"height\":10,\"color\":[0,0,0,255]}}\n"
      "{\"v\":1,\"seq\":3,\"op\":\"delete\",\"id\":1}\n";
  EXPECT_EQ(canvas_poc_document_apply_ndjson(document, replay.data(),
                                              replay.size()),
            CANVAS_POC_STATUS_SEQUENCE_ERROR);
  uint64_t sequence = 42;
  EXPECT_EQ(canvas_poc_document_sequence(document, &sequence),
            CANVAS_POC_STATUS_OK);
  EXPECT_EQ(sequence, 0U);
  EXPECT_EQ(canvas_poc_document_destroy(document), CANVAS_POC_STATUS_OK);
  EXPECT_EQ(canvas_poc_runtime_destroy(runtime), CANVAS_POC_STATUS_OK);
}

TEST(CApiTest, OffscreenViewRendersIntoCallerProvidedRgbaBuffer) {
  canvas_poc_handle_t runtime = 0;
  canvas_poc_handle_t document = 0;
  canvas_poc_handle_t view = 0;
  auto runtime_config = RuntimeConfig();
  auto document_config = DocumentConfig();
  ASSERT_EQ(canvas_poc_runtime_create(&runtime_config, &runtime),
            CANVAS_POC_STATUS_OK);
  ASSERT_EQ(canvas_poc_document_create(runtime, &document_config, &document),
            CANVAS_POC_STATUS_OK);
  const std::string replay =
      "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":1,\"type\":\"rect\",\"order\":1,\"x\":0,\"y\":0,\"width\":10,\"height\":10,\"color\":[0,0,0,255]}}\n";
  ASSERT_EQ(canvas_poc_document_apply_ndjson(document, replay.data(),
                                              replay.size()),
            CANVAS_POC_STATUS_OK);
  canvas_poc_view_config_v1 view_config{};
  view_config.struct_size = sizeof(view_config);
  view_config.abi_version = CANVAS_POC_ABI_VERSION;
  view_config.width = 800;
  view_config.height = 600;
  view_config.device_pixel_ratio = 1;
  ASSERT_EQ(canvas_poc_view_create_offscreen(document, &view_config, &view),
            CANVAS_POC_STATUS_OK);
  ASSERT_EQ(canvas_poc_view_render(view), CANVAS_POC_STATUS_OK);
  size_t required = 0;
  EXPECT_EQ(canvas_poc_view_read_rgba(view, nullptr, 0, &required),
            CANVAS_POC_STATUS_BUFFER_TOO_SMALL);
  EXPECT_EQ(required, 800U * 600U * 4U);
  std::vector<uint8_t> rgba(required);
  EXPECT_EQ(canvas_poc_view_read_rgba(view, rgba.data(), rgba.size(), &required),
            CANVAS_POC_STATUS_OK);
  EXPECT_EQ(rgba[3], 255U);
  EXPECT_EQ(canvas_poc_view_destroy(view), CANVAS_POC_STATUS_OK);
  EXPECT_EQ(canvas_poc_view_render(view), CANVAS_POC_STATUS_INVALID_HANDLE);
  EXPECT_EQ(canvas_poc_document_destroy(document), CANVAS_POC_STATUS_OK);
  EXPECT_EQ(canvas_poc_runtime_destroy(runtime), CANVAS_POC_STATUS_OK);
}

TEST(CApiTest, RuntimeDocumentViewLifecycleRepeatsOneHundredTimes) {
  for (int iteration = 0; iteration < 100; ++iteration) {
    canvas_poc_handle_t runtime = 0;
    canvas_poc_handle_t document = 0;
    canvas_poc_handle_t view = 0;
    auto runtime_config = RuntimeConfig();
    auto document_config = DocumentConfig();
    canvas_poc_view_config_v1 view_config{};
    view_config.struct_size = sizeof(view_config);
    view_config.abi_version = CANVAS_POC_ABI_VERSION;
    view_config.width = 32;
    view_config.height = 32;
    view_config.device_pixel_ratio = 1;
    ASSERT_EQ(canvas_poc_runtime_create(&runtime_config, &runtime),
              CANVAS_POC_STATUS_OK);
    ASSERT_EQ(canvas_poc_document_create(runtime, &document_config, &document),
              CANVAS_POC_STATUS_OK);
    ASSERT_EQ(canvas_poc_view_create_offscreen(document, &view_config, &view),
              CANVAS_POC_STATUS_OK);
    ASSERT_EQ(canvas_poc_view_destroy(view), CANVAS_POC_STATUS_OK);
    ASSERT_EQ(canvas_poc_document_destroy(document), CANVAS_POC_STATUS_OK);
    ASSERT_EQ(canvas_poc_runtime_destroy(runtime), CANVAS_POC_STATUS_OK);
  }
}

}  // namespace
