#include "canvas/poc03/large_scene.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace canvas::poc03 {
namespace {

NodeRecord MakeNode(uint64_t id, uint32_t order, Bounds bounds,
                    NodeType type = NodeType::kShape) {
  return NodeRecord{id, order, type, bounds, 0xff123456U, id % 7U, 1U,
                    false};
}

ChangeSet ApplyOrDie(Document* document, Operation operation) {
  ChangeSet changes;
  std::string error;
  EXPECT_TRUE(document->Apply(operation, &changes, &error)) << error;
  return changes;
}

TEST(NumericContract, RejectsNonFiniteAndCanonicalizesNegativeZero) {
  Document document;
  NodeRecord invalid = MakeNode(
      1U, 0U,
      Bounds{0.0F, 0.0F, std::numeric_limits<float>::infinity(), 10.0F});
  ChangeSet changes;
  std::string error;
  EXPECT_FALSE(document.Apply(
      Operation{OperationKind::kCreate, invalid.id, invalid}, &changes,
      &error));
  EXPECT_EQ(document.revision(), 0U);

  Document negative_zero;
  Document positive_zero;
  NodeRecord first = MakeNode(1U, 0U, Bounds{-0.0F, -0.0F, 10.0F, 10.0F});
  NodeRecord second = MakeNode(1U, 0U, Bounds{0.0F, 0.0F, 10.0F, 10.0F});
  ApplyOrDie(&negative_zero, {OperationKind::kCreate, 1U, first});
  ApplyOrDie(&positive_zero, {OperationKind::kCreate, 1U, second});
  EXPECT_EQ(negative_zero.Digest(), positive_zero.Digest());
}

TEST(Generator, IsDeterministicAndMixed) {
  const GeneratorConfig config{10000U, 1234567U, 1000U, 32.0F};
  const Document first = GenerateDocument(config);
  const Document second = GenerateDocument(config);
  EXPECT_EQ(first.Digest(), second.Digest());
  std::set<NodeType> types;
  for (const NodeRecord* record : first.OrderedRecords()) {
    types.insert(record->type);
  }
  EXPECT_EQ(types.size(), 5U);
  EXPECT_EQ(first.Digest(), "12efa0bb0cb20780b1288f5d12712699");
}

TEST(Generator, DigestDoesNotDependOnInsertionOrContainerIterationOrder) {
  Document ascending;
  Document descending;
  for (uint64_t id = 1U; id <= 50U; ++id) {
    NodeRecord node = MakeNode(id, static_cast<uint32_t>(id),
        Bounds{static_cast<float>(id), 0.0F,
               static_cast<float>(id) + 10.0F, 10.0F});
    ApplyOrDie(&ascending, {OperationKind::kCreate, id, node});
  }
  for (uint64_t id = 50U; id >= 1U; --id) {
    NodeRecord node = MakeNode(id, static_cast<uint32_t>(id),
        Bounds{static_cast<float>(id), 0.0F,
               static_cast<float>(id) + 10.0F, 10.0F});
    ApplyOrDie(&descending, {OperationKind::kCreate, id, node});
  }
  EXPECT_EQ(ascending.Digest(), descending.Digest());
  EXPECT_EQ(SceneCompiler().CompileFull(ascending).Digest(),
            SceneCompiler().CompileFull(descending).Digest());
}

TEST(Operations, AreAtomicAndRevisionBound) {
  Document document;
  NodeRecord node = MakeNode(7U, 3U, Bounds{1.0F, 2.0F, 11.0F, 12.0F});
  const ChangeSet created = ApplyOrDie(
      &document, {OperationKind::kCreate, node.id, node});
  EXPECT_EQ(created.before_revision, 0U);
  EXPECT_EQ(created.after_revision, 1U);
  const std::string digest = document.Digest();
  ChangeSet rejected;
  std::string error;
  EXPECT_FALSE(document.Apply(
      Operation{OperationKind::kCreate, node.id, node}, &rejected, &error));
  EXPECT_EQ(document.Digest(), digest);
  EXPECT_EQ(document.revision(), 1U);
  EXPECT_FALSE(document.Apply(
      Operation{static_cast<OperationKind>(255U), node.id, node},
      &rejected, &error));
  EXPECT_EQ(document.Digest(), digest);
}

TEST(SceneCompiler, FullAndIncrementalCreateUpdateDeleteReorderAgree) {
  Document document;
  SceneCompiler compiler;
  RuntimeScene scene = compiler.CompileFull(document);
  std::string error;
  NodeRecord first = MakeNode(1U, 2U, Bounds{0.0F, 0.0F, 10.0F, 10.0F});
  NodeRecord second = MakeNode(2U, 1U, Bounds{20.0F, 0.0F, 30.0F, 10.0F},
                               NodeType::kStroke);
  std::vector<Operation> operations{
      {OperationKind::kCreate, 1U, first},
      {OperationKind::kCreate, 2U, second},
  };
  first.bounds = Bounds{5.0F, 5.0F, 15.0F, 15.0F};
  ++first.content_revision;
  operations.push_back({OperationKind::kUpdate, 1U, first});
  second.order = 4U;
  operations.push_back({OperationKind::kReorder, 2U, second});
  operations.push_back({OperationKind::kDelete, 1U, std::nullopt});
  for (const Operation& operation : operations) {
    ChangeSet changes = ApplyOrDie(&document, operation);
    CompileDiagnostics diagnostics;
    ASSERT_TRUE(compiler.ApplyIncremental(document, changes, &scene,
                                          &diagnostics, &error));
    const RuntimeScene oracle = compiler.CompileFull(document);
    EXPECT_EQ(scene.Digest(), oracle.Digest());
    if (scene.active_count() != 0U) {
      EXPECT_EQ(scene.ContentBounds(), oracle.ContentBounds());
      const ViewState view{1U, document.revision(), 1U,
          Bounds{-100.0F, -100.0F, 100.0F, 100.0F},
          1.0F, 1.0F, 200U, 200U};
      for (const auto& probe : std::vector<std::pair<float, float>>{
               {1.0F, 1.0F}, {7.0F, 7.0F}, {25.0F, 5.0F}}) {
        EXPECT_EQ(HitTest(scene, view, probe.first, probe.second, 0.5F),
                  HitTest(oracle, view, probe.first, probe.second, 0.5F));
      }
    }
  }
}

TEST(SceneCompiler, HintsCanBeMissingExpandedStaleOrCorrupt) {
  enum class Variant { kCorrect, kMissing, kEmpty, kExpanded, kStale, kCorrupt };
  for (const Variant variant : {Variant::kCorrect, Variant::kMissing,
                                Variant::kEmpty, Variant::kExpanded,
                                Variant::kStale, Variant::kCorrupt}) {
    Document document = GenerateDocument({100U, 42U, 10U, 32.0F});
    SceneCompiler compiler;
    RuntimeScene scene = compiler.CompileFull(document);
    NodeRecord changed = *document.Find(50U);
    changed.bounds = Bounds::Union(
        changed.bounds, Bounds{1000.0F, 1000.0F, 1010.0F, 1010.0F});
    ++changed.content_revision;
    ChangeSet changes = ApplyOrDie(
        &document, {OperationKind::kUpdate, changed.id, changed});
    if (variant == Variant::kMissing) {
      changes.hints.reset();
    } else if (variant == Variant::kEmpty) {
      changes.hints->world_dirty.reset();
    } else if (variant == Variant::kExpanded) {
      changes.hints->world_dirty = changes.hints->world_dirty->Expanded(100.0F);
    } else if (variant == Variant::kStale) {
      changes.hints->after_revision = 0U;
    } else {
      changes.hints->world_dirty = Bounds{0.0F, 0.0F, 1.0F, 1.0F};
    }
    CompileDiagnostics diagnostics;
    std::string error;
    ASSERT_TRUE(compiler.ApplyIncremental(document, changes, &scene,
                                          &diagnostics, &error));
    EXPECT_EQ(scene.Digest(), compiler.CompileFull(document).Digest());
    if (variant == Variant::kEmpty || variant == Variant::kStale ||
        variant == Variant::kCorrupt) {
      EXPECT_EQ(diagnostics.rejected_hints, 1U);
    }
  }
}

TEST(SceneCompiler, InvalidSemanticChangeSafelyFallsBack) {
  Document document = GenerateDocument({100U, 7U, 10U, 32.0F});
  SceneCompiler compiler;
  RuntimeScene scene = compiler.CompileFull(document);
  NodeRecord changed = *document.Find(5U);
  ++changed.content_revision;
  ChangeSet changes = ApplyOrDie(
      &document, {OperationKind::kUpdate, changed.id, changed});
  changes.semantic_changes[0].after->rgba ^= 1U;
  CompileDiagnostics diagnostics;
  std::string error;
  ASSERT_TRUE(compiler.ApplyIncremental(document, changes, &scene,
                                        &diagnostics, &error));
  EXPECT_EQ(diagnostics.full_fallbacks, 1U);
  EXPECT_EQ(scene.Digest(), compiler.CompileFull(document).Digest());
}

TEST(SceneCompiler, CorruptBeforeStateSafelyFallsBack) {
  Document document = GenerateDocument({100U, 8U, 10U, 32.0F});
  SceneCompiler compiler;
  RuntimeScene scene = compiler.CompileFull(document);
  NodeRecord changed = *document.Find(5U);
  ++changed.content_revision;
  ChangeSet changes = ApplyOrDie(
      &document, {OperationKind::kUpdate, changed.id, changed});
  changes.semantic_changes[0].before->bounds =
      Bounds{500.0F, 500.0F, 510.0F, 510.0F};
  CompileDiagnostics diagnostics;
  std::string error;
  ASSERT_TRUE(compiler.ApplyIncremental(document, changes, &scene,
                                        &diagnostics, &error));
  EXPECT_EQ(diagnostics.full_fallbacks, 1U);
  EXPECT_EQ(scene.Digest(), compiler.CompileFull(document).Digest());
}

TEST(SceneCompiler, SinglePropertyUpdateDoesNotScanHundredThousandRecords) {
  Document document = GenerateDocument({100000U, 99U, 1000U, 32.0F});
  SceneCompiler compiler;
  RuntimeScene scene = compiler.CompileFull(document);
  NodeRecord changed = *document.Find(50000U);
  changed.rgba ^= 0x00010101U;
  ++changed.content_revision;
  ChangeSet changes = ApplyOrDie(
      &document, {OperationKind::kUpdate, changed.id, changed});
  CompileDiagnostics diagnostics;
  std::string error;
  ASSERT_TRUE(compiler.ApplyIncremental(document, changes, &scene,
                                        &diagnostics, &error));
  EXPECT_EQ(diagnostics.records_touched, 1U);
  EXPECT_EQ(diagnostics.spatial_records_touched, 1U);
  EXPECT_EQ(diagnostics.order_records_visited, 0U);
  EXPECT_EQ(diagnostics.full_fallbacks, 0U);
}

TEST(SpatialIndex, QueryMatchesBruteForceAndCandidateGate) {
  const Document document = GenerateDocument({100000U, 123U, 1000U, 32.0F});
  const RuntimeScene scene = SceneCompiler().CompileFull(document);
  const ViewState view{1U, 1U, 1U,
      Bounds{12000.0F, 400.0F, 14560.0F, 1840.0F},
      0.75F, 1.0F, 1920U, 1080U};
  const ViewQueryResult result = QueryView(scene, view, std::nullopt);
  std::vector<uint64_t> actual;
  for (const uint32_t slot : result.visible) {
    actual.push_back(scene.RecordAt(slot)->id);
  }
  std::vector<uint64_t> oracle;
  for (const NodeRecord* record : document.OrderedRecords()) {
    if (record->bounds.Intersects(view.world_viewport)) {
      oracle.push_back(record->id);
    }
  }
  EXPECT_EQ(actual, oracle);
  EXPECT_LE(result.candidates.size(), 5000U);
}

TEST(ViewState, TwoViewsKeepQueriesDamageAndKeysIsolated) {
  const RuntimeScene scene = SceneCompiler().CompileFull(
      GenerateDocument({10000U, 77U, 1000U, 32.0F}));
  const ViewState main{1U, 1U, 4U, Bounds{0.0F, 0.0F, 1000.0F, 600.0F},
                       1.0F, 2.0F, 2000U, 1200U};
  const ViewState minimap{2U, 3U, 9U,
      Bounds{10000.0F, 0.0F, 20000.0F, 2000.0F},
      0.1F, 1.0F, 1000U, 200U};
  const ViewQueryResult first = QueryView(
      scene, main, Bounds{10.0F, 10.0F, 20.0F, 20.0F});
  const ViewQueryResult second = QueryView(scene, minimap, std::nullopt);
  EXPECT_NE(first.visible, second.visible);
  EXPECT_NE(first.scale_bucket, second.scale_bucket);
  EXPECT_EQ(first.screen_damage,
            (Bounds{20.0F, 20.0F, 40.0F, 40.0F}));
  const TileKey first_key{main.view_id, scene.source_revision(),
                          main.target_generation, 1U, first.scale_bucket,
                          1U, 0, 0};
  const TileKey second_key{minimap.view_id, scene.source_revision(),
                           minimap.target_generation, 1U,
                           second.scale_bucket, 1U, 0, 0};
  EXPECT_FALSE(first_key == second_key);
}

TEST(HitTest, GeometryQuerySelectionPolicyAndSnapStaySeparate) {
  Document document;
  NodeRecord bottom = MakeNode(1U, 1U, Bounds{0.0F, 0.0F, 20.0F, 20.0F});
  NodeRecord top = MakeNode(2U, 2U, Bounds{5.0F, 5.0F, 25.0F, 25.0F});
  top.locked = true;
  ApplyOrDie(&document, {OperationKind::kCreate, bottom.id, bottom});
  ApplyOrDie(&document, {OperationKind::kCreate, top.id, top});
  const RuntimeScene scene = SceneCompiler().CompileFull(document);
  const ViewState view{1U, 1U, 1U,
      Bounds{-10.0F, -10.0F, 50.0F, 50.0F}, 1.0F, 1.0F, 60U, 60U};
  const std::vector<uint64_t> hits = HitTest(scene, view, 10.0F, 10.0F, 0.0F);
  ASSERT_EQ(hits, (std::vector<uint64_t>{2U, 1U}));
  EXPECT_EQ(SelectFirstUnlocked(scene, hits), 1U);
  const ViewQueryResult query = QueryView(scene, view, std::nullopt);
  EXPECT_EQ(SnapNearestX(scene, query.visible, 4.5F, 1.0F), 5.0F);
}

TEST(FrameGraph, KeepsLogicalContractAndAllowsPhysicalElision) {
  const RuntimeScene scene = SceneCompiler().CompileFull(
      GenerateDocument({50U, 12U, 10U, 32.0F}));
  const ViewState view{1U, 1U, 1U, Bounds{0.0F, 0.0F, 320.0F, 320.0F},
                       1.0F, 1.0F, 320U, 320U};
  const ViewQueryResult query = QueryView(scene, view, std::nullopt);
  FrameGraph graph = BuildFrame(
      scene, query, {{1001U}, {1002U}, {1003U}, {1004U}, {1005U}});
  ASSERT_EQ(graph.logical_passes.size(), 7U);
  EXPECT_TRUE(graph.logical_passes[3].reserved);
  EXPECT_TRUE(graph.logical_passes[3].item_ids.empty());
  EXPECT_EQ(graph.logical_passes[5].item_ids,
            (std::vector<uint64_t>{1004U}));
  EXPECT_EQ(graph.logical_passes[6].item_ids,
            (std::vector<uint64_t>{1005U}));
  const std::string visual_digest = graph.VisualDigest();
  const std::vector<uint64_t> draw_list = ComposeSceneDrawList(graph);
  EXPECT_FALSE(draw_list.empty());
  OptimizeFrameGraph(&graph);
  EXPECT_LT(graph.physical_passes.size(), graph.logical_passes.size());
  EXPECT_EQ(graph.VisualDigest(), visual_digest);
  EXPECT_EQ(ComposeSceneDrawList(graph), draw_list);
}

TEST(TileCache, EnforcesStrictKeyBudgetInvalidationAndDeviceLoss) {
  TileCache cache(1024U);
  const TileKey first{1U, 10U, 1U, 2U, 8U, 1U, 0, 0};
  const TileKey second{1U, 10U, 1U, 2U, 8U, 1U, 1, 0};
  cache.Put(first, 600U);
  cache.Put(second, 600U);
  EXPECT_LE(cache.stats().bytes, 1024U);
  EXPECT_EQ(cache.stats().evictions, 1U);
  EXPECT_FALSE(cache.Find(first));
  EXPECT_TRUE(cache.Find(second));
  const TileKey different_scale{1U, 10U, 1U, 2U, 9U, 1U, 1, 0};
  EXPECT_FALSE(cache.Find(different_scale));
  cache.InvalidateWorld(
      1U, Bounds{256.0F, 0.0F, 511.0F, 255.0F}, 256.0F);
  EXPECT_FALSE(cache.Find(second));
  cache.Put(TileKey{1U, 10U, 1U, 2U, 8U, 1U, 0, 0}, 100U);
  cache.DeviceLost(2U);
  EXPECT_EQ(cache.stats().bytes, 0U);
  EXPECT_EQ(cache.device_generation(), 2U);
  cache.Put(first, 100U);
  EXPECT_EQ(cache.stats().bytes, 0U);
}

TEST(Scheduler, CoalescesBurstRejectsOldGenerationAndIsolatesViews) {
  DeterministicFrameScheduler scheduler;
  for (uint64_t revision = 1; revision <= 100U; ++revision) {
    scheduler.Invalidate(FrameInvalidation{
        1U, revision, revision, 0U, 7U,
        static_cast<uint32_t>(InvalidationReason::kDocument)});
  }
  scheduler.Invalidate(FrameInvalidation{
      2U, 5U, 2U, 0U, 3U,
      static_cast<uint32_t>(InvalidationReason::kView)});
  EXPECT_EQ(scheduler.pending_callback_count(), 2U);
  const auto main = scheduler.Pump(1U, 7U);
  ASSERT_TRUE(main.has_value());
  EXPECT_EQ(main->minimum_document_revision, 100U);
  EXPECT_FALSE(scheduler.Present(*main, 8U));
  EXPECT_TRUE(scheduler.Present(*main, 7U));
  EXPECT_EQ(scheduler.last_presented_revision(1U), 100U);
  EXPECT_EQ(scheduler.pending_callback_count(), 1U);
  EXPECT_FALSE(scheduler.Pump(2U, 4U).has_value());
  scheduler.DestroyView(2U);
  EXPECT_EQ(scheduler.pending_callback_count(), 0U);
}

TEST(Recovery, CacheAndDeviceLossNeverChangeDocumentOrSceneDigest) {
  const Document document = GenerateDocument({1000U, 1U, 100U, 32.0F});
  const RuntimeScene scene = SceneCompiler().CompileFull(document);
  const std::string document_digest = document.Digest();
  const std::string scene_digest = scene.Digest();
  TileCache cache(1024U * 1024U);
  cache.Put(TileKey{1U, scene.source_revision(), 1U, 1U, 8U, 1U, 0, 0},
            4096U);
  cache.Clear();
  cache.DeviceLost(2U);
  EXPECT_EQ(document.Digest(), document_digest);
  EXPECT_EQ(scene.Digest(), scene_digest);
}

}  // namespace
}  // namespace canvas::poc03
