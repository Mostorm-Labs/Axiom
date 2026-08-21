#include "canvas/foundation/result.hpp"
#include "canvas/scene/scene.hpp"
#include "canvas/scene/testing/fake_render_scene.hpp"
#include "canvas/scene/testing/fake_spatial_index.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using canvas::CompiledSceneDelta;
using canvas::CompiledSceneSnapshot;
using canvas::ContentRevision;
using canvas::HitGeometryRef;
using canvas::ObjectId;
using canvas::RenderPayloadRef;
using canvas::Scene;
using canvas::SceneMutation;
using canvas::SceneMutationKind;
using canvas::SceneOrderKey;
using canvas::SceneRecord;
using canvas::SceneRecordFlags;
using canvas::SceneRevision;
using canvas::WorldRect;
using canvas::foundation::ErrorCode;
using canvas::testing::FakeRenderScene;
using canvas::testing::FakeSpatialIndex;

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

SceneRecord makeRecord(std::uint64_t id,
                       std::uint64_t order,
                       WorldRect bounds = WorldRect{0.0F, 0.0F, 10.0F, 10.0F}) {
    return SceneRecord{
        .objectId = ObjectId::fromUint64(id),
        .orderKey = SceneOrderKey(order),
        .flags = SceneRecordFlags::kVisible,
        .worldBounds = bounds,
        .contentRevision = ContentRevision(1),
        .renderPayload = RenderPayloadRef{static_cast<std::uint32_t>(id), 1},
        .hitGeometry = HitGeometryRef{static_cast<std::uint32_t>(id), 1},
    };
}

struct Fixture final {
    Fixture() {
        auto render = std::make_unique<FakeRenderScene>();
        renderRaw = render.get();
        auto spatial = std::make_unique<FakeSpatialIndex>();
        spatialRaw = spatial.get();
        scene = std::make_unique<Scene>(std::move(render), std::move(spatial));
    }

    bool seed() {
        CompiledSceneSnapshot snapshot{
            .sourceRevision = SceneRevision(1),
            .records = {makeRecord(1, 20), makeRecord(2, 10)},
        };
        return scene->replace(std::move(snapshot)).hasValue();
    }

    FakeRenderScene* renderRaw = nullptr;
    FakeSpatialIndex* spatialRaw = nullptr;
    std::unique_ptr<Scene> scene;
};

struct StateSnapshot final {
    SceneRevision sceneRevision;
    std::vector<SceneRecord> sceneRecords;
    std::uint64_t renderDigest = 0;
    std::uint64_t spatialDigest = 0;
    std::uint64_t renderCommits = 0;
    std::uint64_t spatialCommits = 0;
    canvas::DamageSet damage;
    canvas::SceneCommitDiagnostics commitDiagnostics;
};

StateSnapshot capture(const Fixture& fixture) {
    return StateSnapshot{
        .sceneRevision = fixture.scene->revision(),
        .sceneRecords = std::vector<SceneRecord>(fixture.scene->read().records().begin(),
                                                 fixture.scene->read().records().end()),
        .renderDigest = fixture.renderRaw->stateDigest(),
        .spatialDigest = fixture.spatialRaw->stateDigest(),
        .renderCommits = fixture.renderRaw->diagnostics().commitCount,
        .spatialCommits = fixture.spatialRaw->diagnostics().commitCount,
        .damage = fixture.scene->collectDamage(SceneRevision(0), SceneRevision(100)),
        .commitDiagnostics = fixture.scene->commitDiagnostics(),
    };
}

void expectSameState(TestContext& context, const Fixture& fixture, const StateSnapshot& before) {
    const StateSnapshot after = capture(fixture);
    EXPECT(context, after.sceneRevision == before.sceneRevision);
    EXPECT(context, after.sceneRecords == before.sceneRecords);
    EXPECT(context, after.renderDigest == before.renderDigest);
    EXPECT(context, after.spatialDigest == before.spatialDigest);
    EXPECT(context, after.renderCommits == before.renderCommits);
    EXPECT(context, after.spatialCommits == before.spatialCommits);
    EXPECT(context, after.damage.afterExclusive == before.damage.afterExclusive);
    EXPECT(context, after.damage.throughInclusive == before.damage.throughInclusive);
    EXPECT(context, after.damage.fullScene == before.damage.fullScene);
    EXPECT(context, after.damage.rects.size() == before.damage.rects.size());
    for (std::size_t index = 0;
         index < after.damage.rects.size() && index < before.damage.rects.size();
         ++index) {
        EXPECT(context,
               after.damage.rects[index].worldRect == before.damage.rects[index].worldRect);
        EXPECT(context, after.damage.rects[index].reasons == before.damage.rects[index].reasons);
    }
    EXPECT(context,
           after.commitDiagnostics.transactionCount == before.commitDiagnostics.transactionCount);
    EXPECT(context,
           after.commitDiagnostics.revisionStage == before.commitDiagnostics.revisionStage);
}

CompiledSceneDelta makeInsertDelta(std::uint64_t id = 3) {
    const SceneRecord after = makeRecord(id, 30, WorldRect{20.0F, 20.0F, 30.0F, 30.0F});
    return CompiledSceneDelta{
        .beforeRevision = SceneRevision(1),
        .afterRevision = SceneRevision(2),
        .mutations = {SceneMutation{
            .kind = SceneMutationKind::kInsert,
            .objectId = after.objectId,
            .before = std::nullopt,
            .after = after,
        }},
        .hints = std::nullopt,
    };
}

void testSuccessfulReplaceAndCommitOrder(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());
    EXPECT(context, fixture.scene->revision() == SceneRevision(1));
    EXPECT(context, fixture.scene->read().records().size() == 2);
    EXPECT(context, fixture.scene->read().records()[0].objectId == ObjectId::fromUint64(2));
    EXPECT(context, fixture.renderRaw->diagnostics().revision == SceneRevision(1));
    EXPECT(context, fixture.spatialRaw->diagnostics().revision == SceneRevision(1));

    const auto stages = fixture.scene->commitDiagnostics();
    EXPECT(context, stages.recordStoreStage == 1);
    EXPECT(context, stages.renderSceneStage == 2);
    EXPECT(context, stages.spatialIndexStage == 3);
    EXPECT(context, stages.damageStage == 4);
    EXPECT(context, stages.revisionStage == 5);
}

void testSuccessfulInsertUpdateRemove(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());

    auto insertResult = fixture.scene->apply(makeInsertDelta());
    EXPECT(context, insertResult.hasValue());
    EXPECT(context, fixture.scene->read().find(ObjectId::fromUint64(3)) != nullptr);

    const SceneRecord before = *fixture.scene->read().find(ObjectId::fromUint64(1));
    SceneRecord after = before;
    after.worldBounds = WorldRect{-5.0F, -4.0F, 7.0F, 8.0F};
    after.contentRevision = ContentRevision(2);
    CompiledSceneDelta update{
        .beforeRevision = SceneRevision(2),
        .afterRevision = SceneRevision(3),
        .mutations = {SceneMutation{
            .kind = SceneMutationKind::kUpdate,
            .objectId = before.objectId,
            .before = before,
            .after = after,
        }},
        .hints = std::nullopt,
    };
    EXPECT(context, fixture.scene->apply(std::move(update)).hasValue());
    EXPECT(context, fixture.scene->read().find(before.objectId)->worldBounds == after.worldBounds);

    CompiledSceneDelta remove{
        .beforeRevision = SceneRevision(3),
        .afterRevision = SceneRevision(4),
        .mutations = {SceneMutation{
            .kind = SceneMutationKind::kRemove,
            .objectId = after.objectId,
            .before = after,
            .after = std::nullopt,
        }},
        .hints = std::nullopt,
    };
    EXPECT(context, fixture.scene->apply(std::move(remove)).hasValue());
    EXPECT(context, fixture.scene->read().find(after.objectId) == nullptr);
    EXPECT(context, fixture.scene->revision() == SceneRevision(4));
}

void testRenderPrepareFailureIsAtomic(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());
    const StateSnapshot before = capture(fixture);
    fixture.renderRaw->setRejectPrepare(true);

    const auto result = fixture.scene->apply(makeInsertDelta());
    EXPECT(context, !result.hasValue());
    EXPECT(context, result.error().code == ErrorCode::kParticipantRejected);
    expectSameState(context, fixture, before);
    EXPECT(context, fixture.spatialRaw->diagnostics().prepareCount == 1);
}

void testSpatialPrepareFailureIsAtomic(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());
    const StateSnapshot before = capture(fixture);
    fixture.spatialRaw->setRejectPrepare(true);

    const auto result = fixture.scene->apply(makeInsertDelta());
    EXPECT(context, !result.hasValue());
    EXPECT(context, result.error().code == ErrorCode::kParticipantRejected);
    expectSameState(context, fixture, before);
    EXPECT(context, fixture.renderRaw->diagnostics().prepareCount == 2);
}

void testReplacePrepareFailuresAreAtomic(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());
    StateSnapshot before = capture(fixture);
    CompiledSceneSnapshot replacement{
        .sourceRevision = SceneRevision(2),
        .records = {makeRecord(10, 10)},
    };

    fixture.renderRaw->setRejectPrepare(true);
    auto renderResult = fixture.scene->replace(replacement);
    EXPECT(context, !renderResult.hasValue());
    EXPECT(context, renderResult.error().code == ErrorCode::kParticipantRejected);
    expectSameState(context, fixture, before);

    fixture.renderRaw->setRejectPrepare(false);
    fixture.spatialRaw->setRejectPrepare(true);
    auto spatialResult = fixture.scene->replace(std::move(replacement));
    EXPECT(context, !spatialResult.hasValue());
    EXPECT(context, spatialResult.error().code == ErrorCode::kParticipantRejected);
    expectSameState(context, fixture, before);
}

void testInvalidDeltaNeverPreparesParticipants(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());
    const StateSnapshot before = capture(fixture);
    const auto renderPrepares = fixture.renderRaw->diagnostics().prepareCount;
    const auto spatialPrepares = fixture.spatialRaw->diagnostics().prepareCount;

    CompiledSceneDelta stale = makeInsertDelta();
    stale.beforeRevision = SceneRevision(0);
    auto result = fixture.scene->apply(std::move(stale));
    EXPECT(context, !result.hasValue());
    EXPECT(context, result.error().code == ErrorCode::kInvalidRevision);
    expectSameState(context, fixture, before);
    EXPECT(context, fixture.renderRaw->diagnostics().prepareCount == renderPrepares);
    EXPECT(context, fixture.spatialRaw->diagnostics().prepareCount == spatialPrepares);
}

void testBeforeImageAndRecordValidationAreAtomic(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());
    StateSnapshot before = capture(fixture);

    SceneRecord wrongBefore = *fixture.scene->read().find(ObjectId::fromUint64(1));
    wrongBefore.contentRevision = ContentRevision(99);
    SceneRecord after = wrongBefore;
    after.contentRevision = ContentRevision(100);
    CompiledSceneDelta mismatch{
        .beforeRevision = SceneRevision(1),
        .afterRevision = SceneRevision(2),
        .mutations = {SceneMutation{
            .kind = SceneMutationKind::kUpdate,
            .objectId = wrongBefore.objectId,
            .before = wrongBefore,
            .after = after,
        }},
        .hints = std::nullopt,
    };
    auto mismatchResult = fixture.scene->apply(std::move(mismatch));
    EXPECT(context, !mismatchResult.hasValue());
    EXPECT(context, mismatchResult.error().code == ErrorCode::kBeforeImageMismatch);
    expectSameState(context, fixture, before);

    CompiledSceneDelta invalidBounds = makeInsertDelta();
    invalidBounds.mutations[0].after->worldBounds.right = std::numeric_limits<float>::quiet_NaN();
    const auto boundsResult = fixture.scene->apply(std::move(invalidBounds));
    EXPECT(context, !boundsResult.hasValue());
    EXPECT(context, boundsResult.error().code == ErrorCode::kInvalidRecord);
    expectSameState(context, fixture, before);
}

void testDuplicateAndMissingObjectAreAtomic(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());
    const StateSnapshot before = capture(fixture);

    const auto duplicateResult = fixture.scene->apply(makeInsertDelta(1));
    EXPECT(context, !duplicateResult.hasValue());
    EXPECT(context, duplicateResult.error().code == ErrorCode::kDuplicateObject);
    expectSameState(context, fixture, before);

    const SceneRecord missing = makeRecord(404, 40);
    CompiledSceneDelta removeMissing{
        .beforeRevision = SceneRevision(1),
        .afterRevision = SceneRevision(2),
        .mutations = {SceneMutation{
            .kind = SceneMutationKind::kRemove,
            .objectId = missing.objectId,
            .before = missing,
            .after = std::nullopt,
        }},
        .hints = std::nullopt,
    };
    const auto missingResult = fixture.scene->apply(std::move(removeMissing));
    EXPECT(context, !missingResult.hasValue());
    EXPECT(context, missingResult.error().code == ErrorCode::kMissingObject);
    expectSameState(context, fixture, before);
}

void testMalformedSnapshotIsAtomic(TestContext& context) {
    Fixture fixture;
    EXPECT(context, fixture.seed());
    StateSnapshot before = capture(fixture);

    CompiledSceneSnapshot duplicate{
        .sourceRevision = SceneRevision(2),
        .records = {makeRecord(10, 1), makeRecord(10, 2)},
    };
    const auto duplicateResult = fixture.scene->replace(std::move(duplicate));
    EXPECT(context, !duplicateResult.hasValue());
    EXPECT(context, duplicateResult.error().code == ErrorCode::kDuplicateObject);
    expectSameState(context, fixture, before);

    SceneRecord invalidReference = makeRecord(11, 1);
    invalidReference.renderPayload.generation = 0;
    CompiledSceneSnapshot invalid{
        .sourceRevision = SceneRevision(2),
        .records = {invalidReference},
    };
    const auto invalidResult = fixture.scene->replace(std::move(invalid));
    EXPECT(context, !invalidResult.hasValue());
    EXPECT(context, invalidResult.error().code == ErrorCode::kInvalidReference);
    expectSameState(context, fixture, before);
}

} // namespace

int main() {
    TestContext context;
    testSuccessfulReplaceAndCommitOrder(context);
    testSuccessfulInsertUpdateRemove(context);
    testRenderPrepareFailureIsAtomic(context);
    testSpatialPrepareFailureIsAtomic(context);
    testReplacePrepareFailuresAreAtomic(context);
    testInvalidDeltaNeverPreparesParticipants(context);
    testBeforeImageAndRecordValidationAreAtomic(context);
    testDuplicateAndMissingObjectAreAtomic(context);
    testMalformedSnapshotIsAtomic(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " RF01-0 atomicity expectations failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RF01-0 Scene atomicity tests passed\n";
    return EXIT_SUCCESS;
}
