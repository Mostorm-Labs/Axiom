#include "canvas/scene/scene_binding.hpp"
#include "canvas/scene/testing/fake_render_scene.hpp"
#include "canvas/scene/testing/fake_spatial_index.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

namespace {

using canvas::CompiledSceneDelta;
using canvas::CompiledSceneSnapshot;
using canvas::ContentRevision;
using canvas::HitGeometryRef;
using canvas::ObjectId;
using canvas::RenderPayloadRef;
using canvas::Scene;
using canvas::SceneBinding;
using canvas::SceneOrderKey;
using canvas::SceneRecord;
using canvas::SceneRecordFlags;
using canvas::SceneRevision;
using canvas::SceneSyncDisposition;
using canvas::foundation::Error;
using canvas::foundation::ErrorCode;
using canvas::foundation::Result;
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

SceneRecord makeRecord(std::uint64_t id, std::uint64_t order) {
    return SceneRecord{
        .objectId = ObjectId::fromUint64(id),
        .orderKey = SceneOrderKey(order),
        .flags = SceneRecordFlags::kVisible,
        .worldBounds =
            canvas::WorldRect{static_cast<float>(id), 0.0F, static_cast<float>(id) + 1.0F, 1.0F},
        .contentRevision = ContentRevision(1),
        .renderPayload = RenderPayloadRef{static_cast<std::uint32_t>(id - 1U), 1U},
        .hitGeometry = HitGeometryRef{static_cast<std::uint32_t>(id - 1U), 1U},
    };
}

class TestSource final : public canvas::ICompiledSceneSource {
  public:
    Result<CompiledSceneSnapshot> compileFull() const override {
        if (fullError) {
            return Result<CompiledSceneSnapshot>::failure(*fullError);
        }
        return Result<CompiledSceneSnapshot>::success(fullSnapshot);
    }

    Result<CompiledSceneDelta> compileDelta() const override {
        if (deltaError) {
            return Result<CompiledSceneDelta>::failure(*deltaError);
        }
        return Result<CompiledSceneDelta>::success(delta);
    }

    CompiledSceneSnapshot fullSnapshot;
    CompiledSceneDelta delta;
    std::optional<Error> fullError;
    std::optional<Error> deltaError;
};

std::unique_ptr<Scene> makeScene() {
    return std::make_unique<Scene>(std::make_unique<FakeRenderScene>(),
                                   std::make_unique<FakeSpatialIndex>());
}

void testRebuildAndIncrementalSync(TestContext& context) {
    auto scene = makeScene();
    SceneBinding binding(*scene);
    TestSource source;
    const SceneRecord first = makeRecord(1U, 10U);
    source.fullSnapshot = CompiledSceneSnapshot{
        .sourceRevision = SceneRevision(1U),
        .records = {first},
    };
    const auto rebuilt = binding.rebuild(source);
    EXPECT(context, rebuilt.hasValue());
    EXPECT(context, rebuilt.value().disposition == SceneSyncDisposition::kRebuiltFull);
    EXPECT(context, scene->revision() == SceneRevision(1U));

    const SceneRecord second = makeRecord(2U, 20U);
    source.delta = CompiledSceneDelta{
        .beforeRevision = SceneRevision(1U),
        .afterRevision = SceneRevision(2U),
        .mutations = {canvas::SceneMutation{
            .kind = canvas::SceneMutationKind::kInsert,
            .objectId = second.objectId,
            .before = std::nullopt,
            .after = second,
        }},
        .hints = std::nullopt,
    };
    const auto synchronized = binding.synchronize(source);
    EXPECT(context, synchronized.hasValue());
    EXPECT(context, synchronized.value().disposition == SceneSyncDisposition::kAppliedIncremental);
    EXPECT(context, synchronized.value().apply.recordsTouched == 1U);
    EXPECT(context, scene->revision() == SceneRevision(2U));
    EXPECT(context, scene->read().records().size() == 2U);
}

void testRequiresFullFallbackAndFailurePropagation(TestContext& context) {
    auto scene = makeScene();
    SceneBinding binding(*scene);
    TestSource source;
    const SceneRecord first = makeRecord(1U, 10U);
    source.fullSnapshot = CompiledSceneSnapshot{
        .sourceRevision = SceneRevision(1U),
        .records = {first},
    };
    EXPECT(context, binding.rebuild(source).hasValue());

    const SceneRecord replacement = makeRecord(9U, 90U);
    source.fullSnapshot = CompiledSceneSnapshot{
        .sourceRevision = SceneRevision(2U),
        .records = {replacement},
    };
    source.deltaError = Error{
        ErrorCode::kRequiresFullRebuild,
        "test source cannot compile delta",
    };
    const auto fallback = binding.synchronize(source);
    EXPECT(context, fallback.hasValue());
    EXPECT(context, fallback.value().disposition == SceneSyncDisposition::kRebuiltFull);
    EXPECT(context, fallback.value().incrementalFailure.has_value());
    EXPECT(context, scene->read().find(replacement.objectId) != nullptr);

    source.fullSnapshot.sourceRevision = SceneRevision(3U);
    source.fullError = Error{ErrorCode::kInternalError, "full compile failed"};
    const auto failed = binding.synchronize(source);
    EXPECT(context, !failed.hasValue());
    EXPECT(context, failed.error().code == ErrorCode::kInternalError);
    EXPECT(context, scene->revision() == SceneRevision(2U));
    EXPECT(context, scene->read().find(replacement.objectId) != nullptr);
}

void testNonFallbackDeltaErrorIsReturned(TestContext& context) {
    auto scene = makeScene();
    SceneBinding binding(*scene);
    TestSource source;
    source.fullSnapshot = CompiledSceneSnapshot{
        .sourceRevision = SceneRevision(1U),
        .records = {makeRecord(1U, 10U)},
    };
    EXPECT(context, binding.rebuild(source).hasValue());
    source.deltaError = Error{ErrorCode::kInvalidRevision, "stale source"};
    const auto result = binding.synchronize(source);
    EXPECT(context, !result.hasValue());
    EXPECT(context, result.error().code == ErrorCode::kInvalidRevision);
    EXPECT(context, scene->revision() == SceneRevision(1U));
}

} // namespace

int main() {
    TestContext context;
    testRebuildAndIncrementalSync(context);
    testRequiresFullFallbackAndFailurePropagation(context);
    testNonFallbackDeltaErrorIsReturned(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " RF01-2 SceneBinding expectations failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RF01-2 SceneBinding tests passed\n";
    return EXIT_SUCCESS;
}
