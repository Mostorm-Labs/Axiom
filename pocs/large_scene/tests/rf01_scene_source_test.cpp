#include "canvas/poc03/rf01/poc03_scene_source.hpp"
#include "canvas/scene/direct_render_scene.hpp"
#include "canvas/scene/scene_binding.hpp"
#include "canvas/scene/uniform_grid_spatial_index.hpp"

#include "gtest/gtest.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace canvas::poc03::rf01 {
namespace {

std::uint64_t toUint64(ObjectId objectId) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(objectId.bytes[index]) << (index * 8U);
    }
    return value;
}

WorldRect contentBounds(SceneReadView scene) {
    if (scene.records().empty()) {
        return WorldRect{};
    }
    WorldRect result = scene.records().front().worldBounds;
    for (const SceneRecord& record : scene.records().subspan(1)) {
        result = foundation::unionRects(result, record.worldBounds);
    }
    return result;
}

std::vector<std::uint64_t> oldVisibleIds(const RuntimeScene& scene, const ViewState& view) {
    const ViewQueryResult query = QueryView(scene, view, std::nullopt);
    std::vector<std::uint64_t> result;
    result.reserve(query.visible.size());
    for (std::uint32_t slot : query.visible) {
        const auto record = scene.RecordAt(slot);
        EXPECT_TRUE(record.has_value());
        if (record) {
            result.push_back(record->id);
        }
    }
    return result;
}

std::vector<std::uint64_t> newVisibleIds(const SceneQueryResult& query) {
    std::vector<std::uint64_t> result;
    result.reserve(query.backToFront.size());
    for (ObjectId objectId : query.backToFront) {
        result.push_back(toUint64(objectId));
    }
    return result;
}

TEST(Rf01FullRebuild, Poc03ProjectionMatchesAtAllAcceptanceScales) {
    const Poc03SceneSource source;
    for (std::uint32_t nodeCount : {1000U, 10000U, 50000U, 100000U}) {
        SCOPED_TRACE(nodeCount);
        const Document document = GenerateDocument(GeneratorConfig{
            nodeCount,
            0x43414e5641533033ULL,
            1000U,
            32.0F,
        });
        const RuntimeScene oracle = SceneCompiler().CompileFull(document);
        auto snapshotResult = source.compileFull(document);
        ASSERT_TRUE(snapshotResult.hasValue()) << snapshotResult.error().message;

        auto direct = std::make_unique<DirectRenderScene>();
        DirectRenderScene* directRaw = direct.get();
        auto spatial = std::make_unique<UniformGridSpatialIndex>();
        UniformGridSpatialIndex* spatialRaw = spatial.get();
        Scene scene(std::move(direct), std::move(spatial));
        const auto replaceResult = scene.replace(std::move(snapshotResult.value()));
        ASSERT_TRUE(replaceResult.hasValue()) << replaceResult.error().message;
        EXPECT_EQ(replaceResult.value().recordsTouched, nodeCount);
        EXPECT_EQ(scene.revision().value(), oracle.source_revision());
        EXPECT_EQ(scene.read().records().size(), oracle.active_count());
        ASSERT_FALSE(scene.read().records().empty());
        EXPECT_EQ(scene.read().records().front().renderPayload.slot, 0U);
        EXPECT_EQ(scene.read().records().back().renderPayload.slot, nodeCount - 1U);
        EXPECT_EQ(directRaw->diagnostics().nodeCount, nodeCount);
        EXPECT_EQ(spatialRaw->diagnostics().recordCount, nodeCount);

        const auto digestResult = source.projectedDigest(scene.read(), document);
        ASSERT_TRUE(digestResult.hasValue()) << digestResult.error().message;
        EXPECT_EQ(digestResult.value(), oracle.Digest());

        const Bounds oldBounds = oracle.ContentBounds();
        const WorldRect newBounds = contentBounds(scene.read());
        EXPECT_EQ(newBounds,
                  (WorldRect{oldBounds.left, oldBounds.top, oldBounds.right, oldBounds.bottom}));

        const ViewState fullView{
            1U,
            document.revision(),
            1U,
            oldBounds,
            1.0F,
            1.0F,
            512U,
            384U,
        };
        const auto fullQuery = scene.query(SceneQuery{newBounds});
        ASSERT_TRUE(fullQuery.hasValue()) << fullQuery.error().message;
        EXPECT_EQ(newVisibleIds(fullQuery.value()), oldVisibleIds(oracle, fullView));
        const auto drawList = scene.buildDrawList(fullQuery.value());
        ASSERT_TRUE(drawList.hasValue()) << drawList.error().message;
        ASSERT_EQ(drawList.value().items.size(), fullQuery.value().backToFront.size());
        for (std::size_t index = 0; index < drawList.value().items.size(); ++index) {
            EXPECT_EQ(drawList.value().items[index].objectId, fullQuery.value().backToFront[index]);
        }

        const ViewState rasterView{
            2U,
            document.revision(),
            1U,
            Bounds{0.0F, 0.0F, 512.0F, 384.0F},
            1.0F,
            1.0F,
            512U,
            384U,
        };
        const auto oldRgba = source.referenceRgba(oracle, rasterView);
        const auto newRgba = source.referenceRgba(scene, document, rasterView);
        ASSERT_TRUE(oldRgba.hasValue()) << oldRgba.error().message;
        ASSERT_TRUE(newRgba.hasValue()) << newRgba.error().message;
        EXPECT_EQ(oldRgba.value(), newRgba.value());
    }
}

TEST(Rf01FullRebuild, SpatialPrepareFailurePreservesCommittedScene) {
    const Poc03SceneSource source;
    const Document document = GenerateDocument({1000U, 7U, 100U, 32.0F});
    auto snapshotResult = source.compileFull(document);
    ASSERT_TRUE(snapshotResult.hasValue());

    auto direct = std::make_unique<DirectRenderScene>();
    DirectRenderScene* directRaw = direct.get();
    auto spatial = std::make_unique<UniformGridSpatialIndex>();
    UniformGridSpatialIndex* spatialRaw = spatial.get();
    Scene scene(std::move(direct), std::move(spatial));
    ASSERT_TRUE(scene.replace(std::move(snapshotResult.value())).hasValue());

    const auto beforeDigest = source.projectedDigest(scene.read(), document);
    ASSERT_TRUE(beforeDigest.hasValue());
    const SceneRevision beforeRevision = scene.revision();
    const std::uint64_t beforeRenderCommits = directRaw->diagnostics().commitCount;
    const std::uint64_t beforeSpatialCommits = spatialRaw->diagnostics().commitCount;
    const DamageSet beforeDamage =
        scene.collectDamage(SceneRevision(0), SceneRevision(document.revision() + 2U));

    CompiledSceneSnapshot rejected{
        .sourceRevision = SceneRevision(document.revision() + 1U),
        .records = {SceneRecord{
            .objectId = ObjectId::fromUint64(999999U),
            .orderKey = SceneOrderKey(1U),
            .kind = SceneObjectKind::kShape,
            .flags = SceneRecordFlags::kVisible,
            .worldBounds = WorldRect{0.0F, 0.0F, 1000000000.0F, 1000000000.0F},
            .contentRevision = ContentRevision(1U),
            .renderPayload = RenderPayloadRef{1U, 1U},
            .hitGeometry = HitGeometryRef{1U, 1U},
        }},
    };
    const auto rejectedResult = scene.replace(std::move(rejected));
    ASSERT_FALSE(rejectedResult.hasValue());
    EXPECT_EQ(rejectedResult.error().code, foundation::ErrorCode::kInvalidArgument);
    EXPECT_EQ(scene.revision(), beforeRevision);
    EXPECT_EQ(directRaw->diagnostics().commitCount, beforeRenderCommits);
    EXPECT_EQ(spatialRaw->diagnostics().commitCount, beforeSpatialCommits);
    EXPECT_EQ(scene.read().records().size(), document.active_count());
    const auto afterDigest = source.projectedDigest(scene.read(), document);
    ASSERT_TRUE(afterDigest.hasValue());
    EXPECT_EQ(afterDigest.value(), beforeDigest.value());
    const DamageSet afterDamage =
        scene.collectDamage(SceneRevision(0), SceneRevision(document.revision() + 2U));
    EXPECT_EQ(afterDamage.fullScene, beforeDamage.fullScene);
    EXPECT_EQ(afterDamage.rects.size(), beforeDamage.rects.size());
}

TEST(Rf01Delta, Poc03AdapterAndSceneBindingApplyOperationDelta) {
    Document document;
    SceneCompiler compiler;
    NodeRecord first{
        .id = 1U,
        .order = 10U,
        .type = NodeType::kShape,
        .bounds = Bounds{0.0F, 0.0F, 10.0F, 10.0F},
        .rgba = 0xff123456U,
        .resource_key = 0U,
        .content_revision = 1U,
        .locked = false,
    };
    ChangeSet ignored;
    std::string error;
    ASSERT_TRUE(
        document.Apply(Operation{OperationKind::kCreate, first.id, first}, &ignored, &error))
        << error;

    auto render = std::make_unique<DirectRenderScene>();
    auto spatial = std::make_unique<UniformGridSpatialIndex>();
    Scene scene(std::move(render), std::move(spatial));
    Poc03SceneSource boundSource(document);
    SceneBinding binding(scene);
    const auto rebuilt = binding.rebuild(boundSource);
    ASSERT_TRUE(rebuilt.hasValue()) << rebuilt.error().message;
    ASSERT_EQ(scene.read().records().size(), 1U);

    auto applyAndSynchronize = [&](const Operation& operation) {
        ChangeSet changes;
        EXPECT_TRUE(document.Apply(operation, &changes, &error)) << error;
        boundSource.setChangeSet(&changes);
        const auto synchronized = binding.synchronize(boundSource);
        EXPECT_TRUE(synchronized.hasValue()) << synchronized.error().message;
        if (!synchronized) {
            return;
        }
        EXPECT_EQ(synchronized.value().disposition, SceneSyncDisposition::kAppliedIncremental);
        EXPECT_EQ(synchronized.value().apply.recordsTouched, 1U);
        EXPECT_EQ(scene.revision().value(), document.revision());
        const RuntimeScene oracle = compiler.CompileFull(document);
        const auto digest = boundSource.projectedDigest(scene.read(), document);
        EXPECT_TRUE(digest.hasValue()) << digest.error().message;
        if (digest) {
            EXPECT_EQ(digest.value(), oracle.Digest());
        }
    };

    NodeRecord changed = first;
    changed.bounds = Bounds{20.0F, 10.0F, 30.0F, 20.0F};
    ++changed.content_revision;
    applyAndSynchronize(Operation{OperationKind::kUpdate, changed.id, changed});
    EXPECT_EQ(scene.read().find(ObjectId::fromUint64(first.id))->worldBounds,
              (WorldRect{20.0F, 10.0F, 30.0F, 20.0F}));

    NodeRecord second{
        .id = 2U,
        .order = 5U,
        .type = NodeType::kStroke,
        .bounds = Bounds{-20.0F, -10.0F, -5.0F, 5.0F},
        .rgba = 0xff654321U,
        .resource_key = 2U,
        .content_revision = 1U,
        .locked = false,
    };
    applyAndSynchronize(Operation{OperationKind::kCreate, second.id, second});
    ASSERT_EQ(scene.read().records().size(), 2U);
    EXPECT_EQ(scene.read().records().front().objectId, ObjectId::fromUint64(second.id));

    second.order = 20U;
    applyAndSynchronize(Operation{OperationKind::kReorder, second.id, second});
    EXPECT_EQ(scene.read().records().back().objectId, ObjectId::fromUint64(second.id));

    applyAndSynchronize(Operation{OperationKind::kDelete, first.id, std::nullopt});
    EXPECT_EQ(scene.read().records().size(), 1U);
    EXPECT_EQ(scene.read().find(ObjectId::fromUint64(first.id)), nullptr);
}

TEST(Rf01Delta, CorruptPoc03ChangeSetTriggersFullRebuildFallback) {
    Document document = GenerateDocument({100U, 17U, 10U, 32.0F});
    Poc03SceneSource source(document);
    auto render = std::make_unique<DirectRenderScene>();
    auto spatial = std::make_unique<UniformGridSpatialIndex>();
    Scene scene(std::move(render), std::move(spatial));
    SceneBinding binding(scene);
    ASSERT_TRUE(binding.rebuild(source).hasValue());

    NodeRecord changed = *document.Find(50U);
    changed.rgba ^= 0x00010101U;
    ++changed.content_revision;
    ChangeSet changes;
    std::string error;
    ASSERT_TRUE(
        document.Apply(Operation{OperationKind::kUpdate, changed.id, changed}, &changes, &error))
        << error;
    changes.semantic_changes[0].after->rgba ^= 1U;
    source.setChangeSet(&changes);
    const auto synchronized = binding.synchronize(source);
    ASSERT_TRUE(synchronized.hasValue()) << synchronized.error().message;
    EXPECT_EQ(synchronized.value().disposition, SceneSyncDisposition::kRebuiltFull);
    ASSERT_TRUE(synchronized.value().incrementalFailure.has_value());
    EXPECT_EQ(synchronized.value().incrementalFailure->code,
              foundation::ErrorCode::kRequiresFullRebuild);
    const auto digest = source.projectedDigest(scene.read(), document);
    ASSERT_TRUE(digest.hasValue()) << digest.error().message;
    EXPECT_EQ(digest.value(), SceneCompiler().CompileFull(document).Digest());
}

TEST(Rf01Delta, DeterministicOperationSequenceMatchesFullOracle) {
    Document document = GenerateDocument({1000U, 23U, 100U, 32.0F});
    Poc03SceneSource source(document);
    auto render = std::make_unique<DirectRenderScene>();
    auto spatial = std::make_unique<UniformGridSpatialIndex>();
    Scene scene(std::move(render), std::move(spatial));
    SceneBinding binding(scene);
    ASSERT_TRUE(binding.rebuild(source).hasValue());

    std::vector<std::uint64_t> temporaryIds;
    std::string error;
    for (std::uint32_t index = 0; index < 400U; ++index) {
        Operation operation;
        switch (index % 4U) {
        case 0U: {
            const std::uint64_t id = 1U + (static_cast<std::uint64_t>(index) * 37U) % 1000U;
            NodeRecord changed = *document.Find(id);
            changed.rgba ^= 0x00010101U;
            changed.resource_key += 1U;
            ++changed.content_revision;
            operation = Operation{OperationKind::kUpdate, id, changed};
            break;
        }
        case 1U: {
            const std::uint64_t id = 1U + (static_cast<std::uint64_t>(index) * 53U) % 1000U;
            NodeRecord reordered = *document.Find(id);
            reordered.order = 2000U + index;
            operation = Operation{OperationKind::kReorder, id, reordered};
            break;
        }
        case 2U: {
            const std::uint64_t id = 100000U + index;
            NodeRecord created{
                .id = id,
                .order = 5000U + index,
                .type = (index & 4U) == 0U ? NodeType::kImage : NodeType::kStroke,
                .bounds = Bounds{-100.0F + static_cast<float>(index),
                                 -50.0F,
                                 -80.0F + static_cast<float>(index),
                                 -30.0F},
                .rgba = 0xff112233U ^ index,
                .resource_key = id,
                .content_revision = 1U,
                .locked = false,
            };
            temporaryIds.push_back(id);
            operation = Operation{OperationKind::kCreate, id, created};
            break;
        }
        case 3U: {
            ASSERT_FALSE(temporaryIds.empty());
            const std::uint64_t id = temporaryIds.back();
            temporaryIds.pop_back();
            operation = Operation{OperationKind::kDelete, id, std::nullopt};
            break;
        }
        }

        ChangeSet changes;
        ASSERT_TRUE(document.Apply(operation, &changes, &error)) << error;
        source.setChangeSet(&changes);
        const auto synchronized = binding.synchronize(source);
        ASSERT_TRUE(synchronized.hasValue()) << synchronized.error().message;
        EXPECT_EQ(synchronized.value().disposition, SceneSyncDisposition::kAppliedIncremental);
        EXPECT_EQ(synchronized.value().apply.recordsTouched, 1U);
        const auto digest = source.projectedDigest(scene.read(), document);
        ASSERT_TRUE(digest.hasValue()) << digest.error().message;
        EXPECT_EQ(digest.value(), SceneCompiler().CompileFull(document).Digest());
    }
}

} // namespace
} // namespace canvas::poc03::rf01
