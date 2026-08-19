#include "canvas/scene/direct_render_scene.hpp"
#include "canvas/scene/scene.hpp"
#include "canvas/scene/scene_record_store.hpp"
#include "canvas/scene/uniform_grid_spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using canvas::CompiledSceneDelta;
using canvas::CompiledSceneSnapshot;
using canvas::ContentRevision;
using canvas::DirectRenderScene;
using canvas::HitGeometryRef;
using canvas::ObjectId;
using canvas::PreciseHitRequest;
using canvas::RenderPayloadRef;
using canvas::Scene;
using canvas::SceneMutation;
using canvas::SceneMutationKind;
using canvas::SceneOrderKey;
using canvas::SceneQuery;
using canvas::SceneRecord;
using canvas::SceneRecordFlags;
using canvas::SceneRecordStore;
using canvas::SceneRevision;
using canvas::SpatialMutation;
using canvas::UniformGridSpatialIndex;
using canvas::WorldPoint;
using canvas::WorldRect;
using canvas::foundation::ErrorCode;

struct TestContext final {
    int failures = 0;

    void expect(bool condition, std::string_view expression, int line) {
        if (!condition) {
            std::cerr << "line " << line << ": expectation failed: " << expression << '\n';
            ++failures;
        }
    }
};

#define EXPECT(context, expression) (context).expect((expression), #expression, __LINE__)

SceneRecord makeRecord(std::uint64_t id, std::uint64_t order, WorldRect bounds) {
    return SceneRecord{
        .objectId = ObjectId::fromUint64(id),
        .orderKey = SceneOrderKey(order),
        .flags = SceneRecordFlags::kVisible,
        .worldBounds = bounds,
        .contentRevision = ContentRevision(1),
        .renderPayload = RenderPayloadRef{static_cast<std::uint32_t>(id - 1U), 1U},
        .hitGeometry = HitGeometryRef{static_cast<std::uint32_t>(id - 1U), 1U},
    };
}

void testRecordStoreReplaceAndValidation(TestContext& context) {
    SceneRecordStore store;
    const SceneRecord first = makeRecord(1U, 20U, WorldRect{-20.0F, -10.0F, -5.0F, 5.0F});
    const SceneRecord second = makeRecord(2U, 10U, WorldRect{8.0F, 7.0F, 16.0F, 18.0F});
    auto prepared = store.prepareReplace(std::vector<SceneRecord>{first, second});
    EXPECT(context, prepared.hasValue());
    if (!prepared) {
        return;
    }
    EXPECT(context, prepared.value().records()[0].objectId == second.objectId);
    EXPECT(context, prepared.value().find(first.objectId) != nullptr);
    const WorldRect expectedBounds{-20.0F, -10.0F, 16.0F, 18.0F};
    EXPECT(context, prepared.value().contentBounds() == expectedBounds);
    store.commit(std::move(prepared.value()));
    EXPECT(context, store.records().size() == 2U);
    EXPECT(context, store.find(second.objectId) == &store.records()[0]);
    EXPECT(context, store.estimatedBytes() >= sizeof(SceneRecordStore));

    const auto duplicate = store.prepareReplace(std::vector<SceneRecord>{first, first});
    EXPECT(context, !duplicate.hasValue());
    EXPECT(context, duplicate.error().code == ErrorCode::kDuplicateObject);
    SceneRecord invalid = first;
    invalid.renderPayload.generation = 0U;
    const auto invalidResult = store.prepareReplace(std::vector<SceneRecord>{invalid});
    EXPECT(context, !invalidResult.hasValue());
    EXPECT(context, invalidResult.error().code == ErrorCode::kInvalidReference);
    EXPECT(context, store.records().size() == 2U);
}

void testDirectRenderSceneFullReplace(TestContext& context) {
    DirectRenderScene render;
    const SceneRecord back = makeRecord(1U, 10U, WorldRect{-4.0F, -3.0F, 4.0F, 5.0F});
    const SceneRecord front = makeRecord(2U, 20U, WorldRect{0.0F, 0.0F, 10.0F, 10.0F});
    auto prepared = render.prepareReplace(std::vector<SceneRecord>{back, front}, SceneRevision(7U));
    EXPECT(context, prepared.hasValue());
    if (!prepared) {
        return;
    }
    render.commit(std::move(prepared.value()));
    EXPECT(context, render.diagnostics().revision == SceneRevision(7U));
    EXPECT(context, render.diagnostics().nodeCount == 2U);

    const std::vector<ObjectId> order{back.objectId, front.objectId};
    const auto drawList = render.buildDrawList(order);
    EXPECT(context, drawList.hasValue());
    if (drawList) {
        EXPECT(context, drawList.value().revision == SceneRevision(7U));
        EXPECT(context, drawList.value().items.size() == 2U);
        EXPECT(context, drawList.value().items[0].renderPayload == back.renderPayload);
        EXPECT(context, drawList.value().items[1].renderPayload == front.renderPayload);
    }
    const auto inside =
        render.preciseHitTest(PreciseHitRequest{back.objectId, WorldPoint{0.0F, 0.0F}, 0.0F});
    const auto near =
        render.preciseHitTest(PreciseHitRequest{back.objectId, WorldPoint{5.0F, 5.0F}, 1.0F});
    const auto outside =
        render.preciseHitTest(PreciseHitRequest{back.objectId, WorldPoint{6.1F, 5.0F}, 1.0F});
    EXPECT(context, inside.hasValue() && inside.value().hit);
    EXPECT(context, near.hasValue() && near.value().hit);
    EXPECT(context, outside.hasValue() && !outside.value().hit);

    auto delta =
        render.prepareApply(std::vector<SceneMutation>{}, SceneRevision(7U), SceneRevision(8U));
    EXPECT(context, delta.hasValue());
    if (delta) {
        render.commit(std::move(delta.value()));
        EXPECT(context, render.diagnostics().revision == SceneRevision(8U));
        EXPECT(context, render.diagnostics().nodeCount == 2U);
    }
    const auto stale =
        render.prepareApply(std::vector<SceneMutation>{}, SceneRevision(7U), SceneRevision(9U));
    EXPECT(context, !stale.hasValue());
    EXPECT(context, stale.error().code == ErrorCode::kInvalidRevision);
}

std::vector<ObjectId> bruteForce(std::span<const canvas::SpatialRecord> records,
                                 const WorldRect& query) {
    std::vector<ObjectId> result;
    for (const canvas::SpatialRecord& record : records) {
        if (record.worldBounds.intersects(query)) {
            result.push_back(record.objectId);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void testUniformGridMatchesBruteForce(TestContext& context) {
    bool invalidSizeRejected = false;
    try {
        static_cast<void>(UniformGridSpatialIndex(0.0F));
    } catch (const std::invalid_argument&) {
        invalidSizeRejected = true;
    }
    EXPECT(context, invalidSizeRejected);

    const std::vector<canvas::SpatialRecord> records{
        {ObjectId::fromUint64(1U), WorldRect{-300.0F, -300.0F, -10.0F, -10.0F}},
        {ObjectId::fromUint64(2U), WorldRect{-20.0F, -20.0F, 20.0F, 20.0F}},
        {ObjectId::fromUint64(3U), WorldRect{10.0F, 10.0F, 700.0F, 700.0F}},
        {ObjectId::fromUint64(4U), WorldRect{512.0F, -512.0F, 512.0F, -512.0F}},
    };
    UniformGridSpatialIndex index(256.0F);
    auto prepared = index.prepareReplace(records, SceneRevision(3U));
    EXPECT(context, prepared.hasValue());
    if (!prepared) {
        return;
    }
    index.commit(std::move(prepared.value()));

    for (const WorldRect query : {
             WorldRect{-512.0F, -512.0F, 0.0F, 0.0F},
             WorldRect{-1.0F, -1.0F, 513.0F, 513.0F},
             WorldRect{255.0F, 255.0F, 513.0F, 513.0F},
         }) {
        const auto actual = index.query(query);
        EXPECT(context, actual.hasValue());
        if (!actual) {
            continue;
        }
        std::vector<ObjectId> actualIds = actual.value().candidates;
        std::sort(actualIds.begin(), actualIds.end());
        EXPECT(context, actualIds == bruteForce(records, query));
        EXPECT(context, std::adjacent_find(actualIds.begin(), actualIds.end()) == actualIds.end());
    }

    const auto pointQuery = index.query(WorldRect{512.0F, -512.0F, 512.0F, -512.0F});
    EXPECT(context, pointQuery.hasValue());
    auto delta =
        index.prepareApply(std::vector<SpatialMutation>{}, SceneRevision(3U), SceneRevision(4U));
    EXPECT(context, delta.hasValue());
    if (delta) {
        index.commit(std::move(delta.value()));
        EXPECT(context, index.diagnostics().revision == SceneRevision(4U));
        EXPECT(context, index.diagnostics().recordCount == records.size());
    }
    const auto stale =
        index.prepareApply(std::vector<SpatialMutation>{}, SceneRevision(3U), SceneRevision(5U));
    EXPECT(context, !stale.hasValue());
    EXPECT(context, stale.error().code == ErrorCode::kInvalidRevision);
}

void testSceneFullReplaceAndStaleDrawList(TestContext& context) {
    auto render = std::make_unique<DirectRenderScene>();
    auto spatial = std::make_unique<UniformGridSpatialIndex>();
    Scene scene(std::move(render), std::move(spatial));
    const SceneRecord first = makeRecord(1U, 20U, WorldRect{-10.0F, -10.0F, 10.0F, 10.0F});
    const SceneRecord second = makeRecord(2U, 10U, WorldRect{20.0F, 20.0F, 30.0F, 30.0F});
    auto replace = scene.replace(CompiledSceneSnapshot{
        .sourceRevision = SceneRevision(1U),
        .records = {first, second},
    });
    EXPECT(context, replace.hasValue());
    const auto oldQuery = scene.query(SceneQuery{WorldRect{-50.0F, -50.0F, 50.0F, 50.0F}});
    EXPECT(context, oldQuery.hasValue());
    if (!oldQuery) {
        return;
    }
    EXPECT(context, oldQuery.value().backToFront.size() == 2U);
    EXPECT(context, oldQuery.value().backToFront[0] == second.objectId);
    EXPECT(context, oldQuery.value().backToFront[1] == first.objectId);

    replace = scene.replace(CompiledSceneSnapshot{
        .sourceRevision = SceneRevision(2U),
        .records = {first},
    });
    EXPECT(context, replace.hasValue());
    const auto staleDrawList = scene.buildDrawList(oldQuery.value());
    EXPECT(context, !staleDrawList.hasValue());
    EXPECT(context, staleDrawList.error().code == ErrorCode::kInvalidRevision);

    CompiledSceneDelta delta{
        .beforeRevision = SceneRevision(2U),
        .afterRevision = SceneRevision(3U),
        .mutations = {SceneMutation{
            .kind = SceneMutationKind::kInsert,
            .objectId = second.objectId,
            .after = second,
        }},
    };
    const auto deltaResult = scene.apply(std::move(delta));
    EXPECT(context, deltaResult.hasValue());
    EXPECT(context, scene.revision() == SceneRevision(3U));
    EXPECT(context, scene.read().records().size() == 2U);
    const auto updatedQuery = scene.query(SceneQuery{WorldRect{-50.0F, -50.0F, 50.0F, 50.0F}});
    EXPECT(context, updatedQuery.hasValue());
    if (updatedQuery) {
        EXPECT(context, updatedQuery.value().backToFront[0] == second.objectId);
        EXPECT(context, updatedQuery.value().backToFront[1] == first.objectId);
    }
}

void testSpatialPrepareFailurePreservesScene(TestContext& context) {
    auto render = std::make_unique<DirectRenderScene>();
    DirectRenderScene* renderRaw = render.get();
    auto spatial = std::make_unique<UniformGridSpatialIndex>();
    UniformGridSpatialIndex* spatialRaw = spatial.get();
    Scene scene(std::move(render), std::move(spatial));
    const SceneRecord initial = makeRecord(1U, 10U, WorldRect{-10.0F, -10.0F, 10.0F, 10.0F});
    EXPECT(context,
           scene
               .replace(CompiledSceneSnapshot{
                   .sourceRevision = SceneRevision(1U),
                   .records = {initial},
               })
               .hasValue());
    const std::uint64_t renderCommits = renderRaw->diagnostics().commitCount;
    const std::uint64_t spatialCommits = spatialRaw->diagnostics().commitCount;

    SceneRecord oversized =
        makeRecord(2U, 20U, WorldRect{0.0F, 0.0F, 1'000'000'000.0F, 1'000'000'000.0F});
    const auto rejected = scene.replace(CompiledSceneSnapshot{
        .sourceRevision = SceneRevision(2U),
        .records = {oversized},
    });
    EXPECT(context, !rejected.hasValue());
    EXPECT(context, rejected.error().code == ErrorCode::kInvalidArgument);
    EXPECT(context, scene.revision() == SceneRevision(1U));
    EXPECT(context, scene.read().records().size() == 1U);
    EXPECT(context, scene.read().records()[0] == initial);
    EXPECT(context, renderRaw->diagnostics().commitCount == renderCommits);
    EXPECT(context, spatialRaw->diagnostics().commitCount == spatialCommits);
}

void testDeltaSpatialPrepareFailurePreservesScene(TestContext& context) {
    auto render = std::make_unique<DirectRenderScene>();
    DirectRenderScene* renderRaw = render.get();
    auto spatial = std::make_unique<UniformGridSpatialIndex>();
    UniformGridSpatialIndex* spatialRaw = spatial.get();
    Scene scene(std::move(render), std::move(spatial));
    const SceneRecord initial = makeRecord(1U, 10U, WorldRect{-10.0F, -10.0F, 10.0F, 10.0F});
    EXPECT(context,
           scene
               .replace(CompiledSceneSnapshot{
                   .sourceRevision = SceneRevision(1U),
                   .records = {initial},
               })
               .hasValue());
    const std::uint64_t renderCommits = renderRaw->diagnostics().commitCount;
    const std::uint64_t spatialCommits = spatialRaw->diagnostics().commitCount;
    SceneRecord oversized = initial;
    oversized.worldBounds = WorldRect{0.0F, 0.0F, 1'000'000'000.0F, 1'000'000'000.0F};
    oversized.contentRevision = ContentRevision(2U);
    const auto rejected = scene.apply(CompiledSceneDelta{
        .beforeRevision = SceneRevision(1U),
        .afterRevision = SceneRevision(2U),
        .mutations = {SceneMutation{
            .kind = SceneMutationKind::kUpdate,
            .objectId = initial.objectId,
            .before = initial,
            .after = oversized,
        }},
    });
    EXPECT(context, !rejected.hasValue());
    EXPECT(context, rejected.error().code == ErrorCode::kInvalidArgument);
    EXPECT(context, scene.revision() == SceneRevision(1U));
    EXPECT(context, scene.read().records().size() == 1U);
    EXPECT(context, scene.read().records()[0] == initial);
    EXPECT(context, renderRaw->diagnostics().commitCount == renderCommits);
    EXPECT(context, spatialRaw->diagnostics().commitCount == spatialCommits);
}

} // namespace

int main() {
    TestContext context;
    testRecordStoreReplaceAndValidation(context);
    testDirectRenderSceneFullReplace(context);
    testUniformGridMatchesBruteForce(context);
    testSceneFullReplaceAndStaleDrawList(context);
    testSpatialPrepareFailurePreservesScene(context);
    testDeltaSpatialPrepareFailurePreservesScene(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " RF01-1 full rebuild expectations failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RF01-1 full rebuild tests passed\n";
    return EXIT_SUCCESS;
}
