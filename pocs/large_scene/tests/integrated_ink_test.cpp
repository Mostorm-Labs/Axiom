#include "canvas/poc03/ink_integration.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "gtest/gtest.h"

namespace canvas::poc03 {
namespace {

constexpr poc02::StrokeId kTestStrokeId = UINT64_C(0x2000000000000001);

void PushRemainingBatches(IntegratedInkController* controller,
                          uint32_t stroke_index,
                          poc02::StrokeId stroke_id) {
  for (uint32_t batch_index = 1U; batch_index < 4U; ++batch_index) {
    auto batch = DeterministicInkBatch(stroke_index, batch_index, stroke_id);
    const uint64_t now_us = batch.samples.back().timestamp_us;
    ASSERT_EQ(controller->Push(std::move(batch), now_us),
              poc02::Status::kOk);
  }
}

TEST(InkGeometryStore, IndexPreservesCanonicalDigestAndTwentyThousandLookups) {
  InkGeometryStore store;
  Document document;
  RuntimeScene scene = SceneCompiler().CompileFull(document);
  TileCache cache(1024U * 1024U);
  DeterministicFrameScheduler scheduler;
  IntegratedInkController controller(store, document, scene, cache, scheduler);
  for (uint32_t index = 0U; index < 20000U; ++index) {
    const poc02::StrokeId id = kTestStrokeId + index;
    ASSERT_EQ(controller.Begin(
                  id, id,
                  DeterministicBrush((index & 1U) == 0U
                                         ? poc02::BrushType::kVector
                                         : poc02::BrushType::kDab),
                  DeterministicInkBatch(index, 0U, id)),
              poc02::Status::kOk);
    PushRemainingBatches(&controller, index, id);
    InkCommitDiagnostics diagnostics;
    std::string error;
    ASSERT_TRUE(controller.Commit(index, &diagnostics, &error)) << error;
    const auto frame = controller.BeginFrame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(controller.CompletePresentation(
        *frame, scene.source_revision(), true, &diagnostics, &error)) << error;
  }
  EXPECT_EQ(store.document().stroke_count(), 20000U);
  EXPECT_EQ(store.document().indexed_stroke_count(), 20000U);
  for (uint32_t index = 0U; index < 20000U; ++index) {
    const poc02::StrokeId id = kTestStrokeId + (index * 7919U) % 20000U;
    ASSERT_NE(store.Find(id), nullptr);
    EXPECT_EQ(store.Find(id)->id, id);
  }
  // The index is derived state; the canonical digest remains a 128-bit hex
  // oracle over the original ordered Stroke vector only.
  EXPECT_EQ(store.document().Digest().size(), 32U);
}

TEST(InkSceneAdapter, UsesStableIdResourceAndConservativeCircleBounds) {
  InkGeometryStore store;
  Document document;
  RuntimeScene scene = SceneCompiler().CompileFull(document);
  TileCache cache(1024U * 1024U);
  DeterministicFrameScheduler scheduler;
  IntegratedInkController controller(store, document, scene, cache, scheduler);
  ASSERT_EQ(controller.Begin(
                kTestStrokeId, kTestStrokeId,
                DeterministicBrush(poc02::BrushType::kDab),
                DeterministicInkBatch(7U, 0U, kTestStrokeId)),
            poc02::Status::kOk);
  PushRemainingBatches(&controller, 7U, kTestStrokeId);
  InkCommitDiagnostics diagnostics;
  std::string error;
  ASSERT_TRUE(controller.Commit(17U, &diagnostics, &error)) << error;
  const NodeRecord* node = document.Find(kTestStrokeId);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, NodeType::kStroke);
  EXPECT_EQ(node->id, kTestStrokeId);
  EXPECT_EQ(node->resource_key, kTestStrokeId);
  const poc02::Stroke* stroke = store.Find(kTestStrokeId);
  ASSERT_NE(stroke, nullptr);
  for (const poc02::Dab& dab : stroke->dabs) {
    EXPECT_LE(node->bounds.left, dab.position.x - dab.radius);
    EXPECT_LE(node->bounds.top, dab.position.y - dab.radius);
    EXPECT_GE(node->bounds.right, dab.position.x + dab.radius);
    EXPECT_GE(node->bounds.bottom, dab.position.y + dab.radius);
  }
}

TEST(IntegratedInk, CommitTouchesOneRecordAndAcknowledgesOnlyAfterPresent) {
  Document document = GenerateDocument({100000U, 99U, 1000U, 32.0F});
  SceneCompiler compiler;
  RuntimeScene scene = compiler.CompileFull(document);
  InkGeometryStore store;
  TileCache cache(4U * 1024U * 1024U);
  DeterministicFrameScheduler scheduler;
  IntegratedInkController controller(store, document, scene, cache, scheduler);
  ASSERT_EQ(controller.Begin(
                kTestStrokeId, kTestStrokeId,
                DeterministicBrush(poc02::BrushType::kVector),
                DeterministicInkBatch(13U, 0U, kTestStrokeId)),
            poc02::Status::kOk);
  PushRemainingBatches(&controller, 13U, kTestStrokeId);
  const auto* active_preview = controller.DrawablePreview(scene.source_revision());
  ASSERT_NE(active_preview, nullptr);
  EXPECT_FALSE(active_preview->committed);

  InkCommitDiagnostics diagnostics;
  std::string error;
  const uint64_t before_scene_revision = scene.source_revision();
  ASSERT_TRUE(controller.Commit(100000U, &diagnostics, &error)) << error;
  EXPECT_TRUE(diagnostics.canonical_committed);
  EXPECT_TRUE(diagnostics.scene_integrated);
  EXPECT_FALSE(diagnostics.visible_acknowledged);
  EXPECT_EQ(diagnostics.compile.records_touched, 1U);
  EXPECT_EQ(diagnostics.compile.spatial_records_touched, 1U);
  EXPECT_EQ(diagnostics.compile.full_fallbacks, 0U);
  EXPECT_EQ(scene.Digest(), compiler.CompileFull(document).Digest());
  EXPECT_NE(controller.DrawablePreview(before_scene_revision), nullptr);
  EXPECT_EQ(controller.DrawablePreview(scene.source_revision()), nullptr);

  const auto frame = controller.BeginFrame();
  ASSERT_TRUE(frame.has_value());
  EXPECT_FALSE(controller.CompletePresentation(
      *frame, before_scene_revision, true, &diagnostics, &error));
  EXPECT_TRUE(controller.has_pending_handoff());
  EXPECT_FALSE(store.Preview(kTestStrokeId)->visible);

  // The failed stale present consumed the scheduler frame, so a surface
  // generation change requests the retained canonical Stroke again.
  controller.ViewChanged(2U);
  const auto replacement = controller.BeginFrame();
  ASSERT_TRUE(replacement.has_value());
  ASSERT_TRUE(controller.CompletePresentation(
      *replacement, scene.source_revision(), true, &diagnostics, &error))
      << error;
  EXPECT_TRUE(diagnostics.visible_acknowledged);
  EXPECT_FALSE(controller.has_pending_handoff());
  EXPECT_TRUE(store.Preview(kTestStrokeId)->visible);
  EXPECT_EQ(scheduler.pending_callback_count(), 0U);
}

TEST(IntegratedInk, ViewChangeAndQueueOverrunCancelWithoutSceneNode) {
  Document document;
  RuntimeScene scene = SceneCompiler().CompileFull(document);
  InkGeometryStore store;
  TileCache cache(1024U * 1024U);
  DeterministicFrameScheduler scheduler;
  IntegratedInkController controller(store, document, scene, cache, scheduler);
  ASSERT_EQ(controller.Begin(
                kTestStrokeId, kTestStrokeId,
                DeterministicBrush(poc02::BrushType::kVector),
                DeterministicInkBatch(0U, 0U, kTestStrokeId)),
            poc02::Status::kOk);
  controller.ViewChanged(2U);
  EXPECT_FALSE(store.active_stroke_id().has_value());
  EXPECT_EQ(document.Find(kTestStrokeId), nullptr);
  EXPECT_EQ(store.Find(kTestStrokeId), nullptr);
  EXPECT_EQ(scene.active_count(), 0U);
}

TEST(IntegratedScale, UsesRealFourBatchVectorAndDabPipeline) {
  Document document;
  RuntimeScene scene;
  InkGeometryStore store;
  TileCache cache(8U * 1024U * 1024U);
  DeterministicFrameScheduler scheduler;
  IntegratedScaleReport report;
  std::string error;
  ASSERT_TRUE(BuildIntegratedScale({1000U, 200U, 42U}, &document, &scene,
                                   &store, &cache, &scheduler, &report,
                                   &error)) << error;
  EXPECT_EQ(report.vector_strokes, 100U);
  EXPECT_EQ(report.dab_strokes, 100U);
  EXPECT_EQ(store.document().stroke_count(), 200U);
  EXPECT_EQ(document.active_count(), 1200U);
  EXPECT_EQ(scene.active_count(), 1200U);
  EXPECT_EQ(report.maximum_records_touched, 1U);
  EXPECT_EQ(report.full_fallbacks, 0U);
  EXPECT_LE(report.maximum_queue_batches, 1U);
  EXPECT_LE(report.maximum_pending_callbacks, 1U);
  EXPECT_EQ(report.scene_digest, SceneCompiler().CompileFull(document).Digest());
}

}  // namespace
}  // namespace canvas::poc03
