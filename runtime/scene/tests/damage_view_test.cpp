#include "canvas/scene/damage_tracker.hpp"
#include "canvas/scene/scene_view.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace {

using canvas::CompiledSceneDelta;
using canvas::DamageLimits;
using canvas::DamageReason;
using canvas::DamageSet;
using canvas::DamageTracker;
using canvas::InvalidationHints;
using canvas::SceneMutation;
using canvas::SceneMutationKind;
using canvas::SceneRecord;
using canvas::SceneRevision;
using canvas::SceneViewRegistry;
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

SceneRecord record(std::uint64_t id, WorldRect bounds) {
    return SceneRecord{
        .objectId = canvas::ObjectId::fromUint64(id),
        .worldBounds = bounds,
    };
}

CompiledSceneDelta insertDelta(std::uint64_t before,
                               std::uint64_t after,
                               std::uint64_t id,
                               WorldRect bounds,
                               std::optional<InvalidationHints> hints = std::nullopt) {
    return CompiledSceneDelta{
        .beforeRevision = SceneRevision(before),
        .afterRevision = SceneRevision(after),
        .mutations = {SceneMutation{
            .kind = SceneMutationKind::kInsert,
            .objectId = canvas::ObjectId::fromUint64(id),
            .after = record(id, bounds),
        }},
        .hints = std::move(hints),
    };
}

void testCollapseAndCompact(TestContext& context) {
    DamageTracker tracker(DamageLimits{.maxJournalEntries = 2U,
                                       .maxRectCount = 16U,
                                       .maxBytes = 4096U});
    for (std::uint64_t revision = 1U; revision <= 3U; ++revision) {
        auto prepared = tracker.prepareApply(insertDelta(
            revision - 1U, revision, revision, WorldRect{static_cast<float>(revision), 0.0F,
                                                          static_cast<float>(revision + 1U), 1.0F}));
        EXPECT(context, prepared.hasValue());
        if (prepared) {
            tracker.commit(std::move(prepared.value()));
        }
    }
    const auto diagnostics = tracker.diagnostics();
    EXPECT(context, diagnostics.journalEntries == 1U);
    EXPECT(context, diagnostics.collapseCount == 1U);
    EXPECT(context, diagnostics.rectCount == 1U);
    const DamageSet collected = tracker.collect(SceneRevision(0U), SceneRevision(3U));
    EXPECT(context, collected.fullScene);

    tracker.compactThrough(SceneRevision(3U));
    EXPECT(context, tracker.journalSize() == 0U);
    EXPECT(context, tracker.diagnostics().compactedThrough == SceneRevision(3U));
    EXPECT(context, tracker.collect(SceneRevision(0U), SceneRevision(3U)).fullScene);
}

void testRectAndByteLimits(TestContext& context) {
    DamageTracker rectLimited(DamageLimits{.maxJournalEntries = 32U,
                                           .maxRectCount = 1U,
                                           .maxBytes = 4096U});
    CompiledSceneDelta delta{
        .beforeRevision = SceneRevision(0U),
        .afterRevision = SceneRevision(1U),
        .mutations = {
            SceneMutation{.kind = SceneMutationKind::kInsert,
                          .objectId = canvas::ObjectId::fromUint64(1U),
                          .after = record(1U, WorldRect{0.0F, 0.0F, 1.0F, 1.0F})},
            SceneMutation{.kind = SceneMutationKind::kInsert,
                          .objectId = canvas::ObjectId::fromUint64(2U),
                          .after = record(2U, WorldRect{2.0F, 2.0F, 3.0F, 3.0F})},
        },
    };
    auto prepared = rectLimited.prepareApply(delta);
    EXPECT(context, prepared.hasValue());
    if (prepared) {
        rectLimited.commit(std::move(prepared.value()));
    }
    EXPECT(context, rectLimited.diagnostics().collapseCount == 1U);
    EXPECT(context, rectLimited.diagnostics().rectCount <= 1U);
    EXPECT(context, rectLimited.collect(SceneRevision(0U), SceneRevision(1U)).fullScene);

    DamageTracker byteLimited(DamageLimits{.maxJournalEntries = 32U,
                                           .maxRectCount = 32U,
                                           .maxBytes = 160U});
    auto bytePrepared = byteLimited.prepareApply(insertDelta(
        0U, 1U, 1U, WorldRect{0.0F, 0.0F, 1.0F, 1.0F}));
    EXPECT(context, bytePrepared.hasValue());
    if (bytePrepared) {
        byteLimited.commit(std::move(bytePrepared.value()));
    }
    auto bytePreparedSecond = byteLimited.prepareApply(insertDelta(
        1U, 2U, 2U, WorldRect{2.0F, 0.0F, 3.0F, 1.0F}));
    EXPECT(context, bytePreparedSecond.hasValue());
    if (bytePreparedSecond) {
        byteLimited.commit(std::move(bytePreparedSecond.value()));
    }
    EXPECT(context, byteLimited.diagnostics().collapseCount == 1U);
    EXPECT(context, byteLimited.diagnostics().estimatedBytes <= 96U);
}

void testDefaultPublishedLimits(TestContext& context) {
    DamageTracker entryLimited;
    for (std::uint64_t revision = 1U; revision <= 257U; ++revision) {
        auto prepared = entryLimited.prepareApply(insertDelta(
            revision - 1U,
            revision,
            revision,
            WorldRect{static_cast<float>(revision),
                      0.0F,
                      static_cast<float>(revision + 1U),
                      1.0F}));
        EXPECT(context, prepared.hasValue());
        if (!prepared) {
            return;
        }
        entryLimited.commit(std::move(prepared.value()));
    }
    const auto entryDiagnostics = entryLimited.diagnostics();
    EXPECT(context, entryDiagnostics.collapseCount == 1U);
    EXPECT(context, entryDiagnostics.journalEntries == 1U);
    EXPECT(context, entryDiagnostics.rectCount <= 4096U);
    EXPECT(context, entryDiagnostics.estimatedBytes <= 1024U * 1024U);

    DamageTracker rectLimited(DamageLimits{.maxJournalEntries = 8192U,
                                           .maxRectCount = 4096U,
                                           .maxBytes = 16U * 1024U * 1024U});
    CompiledSceneDelta delta{
        .beforeRevision = SceneRevision(0U),
        .afterRevision = SceneRevision(1U),
    };
    delta.mutations.reserve(4097U);
    for (std::uint64_t id = 1U; id <= 4097U; ++id) {
        delta.mutations.push_back(SceneMutation{
            .kind = SceneMutationKind::kInsert,
            .objectId = canvas::ObjectId::fromUint64(id),
            .after = record(id, WorldRect{static_cast<float>(id),
                                          0.0F,
                                          static_cast<float>(id + 1U),
                                          1.0F}),
        });
    }
    auto rectPrepared = rectLimited.prepareApply(delta);
    EXPECT(context, rectPrepared.hasValue());
    if (rectPrepared) {
        rectLimited.commit(std::move(rectPrepared.value()));
    }
    EXPECT(context, rectLimited.diagnostics().collapseCount == 1U);
    EXPECT(context, rectLimited.diagnostics().rectCount == 1U);
}

void testHintsNeverShrinkAuthoritativeDamage(TestContext& context) {
    DamageTracker tracker;
    const WorldRect authoritative{0.0F, 0.0F, 10.0F, 10.0F};
    InvalidationHints expanded{
        .worldDirty = WorldRect{-5.0F, -5.0F, 15.0F, 15.0F},
        .flags = canvas::InvalidationHintFlags::kLayoutChanged,
        .beforeRevision = SceneRevision(0U),
        .afterRevision = SceneRevision(1U),
    };
    auto accepted = tracker.prepareApply(insertDelta(0U, 1U, 1U, authoritative, expanded));
    EXPECT(context, accepted.hasValue());
    if (accepted) {
        tracker.commit(std::move(accepted.value()));
    }
    EXPECT(context, tracker.diagnostics().rejectedHints == 0U);
    EXPECT(context, tracker.collect(SceneRevision(0U), SceneRevision(1U)).rects.size() == 2U);

    InvalidationHints stale{
        .worldDirty = WorldRect{-1.0F, -1.0F, 20.0F, 20.0F},
        .beforeRevision = SceneRevision(7U),
        .afterRevision = SceneRevision(8U),
    };
    auto rejected = tracker.prepareApply(insertDelta(1U, 2U, 2U, authoritative, stale));
    EXPECT(context, rejected.hasValue());
    if (rejected) {
        tracker.commit(std::move(rejected.value()));
    }
    EXPECT(context, tracker.diagnostics().rejectedHints == 1U);

    InvalidationHints nanHint{
        .worldDirty = WorldRect{0.0F,
                                0.0F,
                                std::numeric_limits<float>::quiet_NaN(),
                                1.0F},
        .beforeRevision = SceneRevision(2U),
        .afterRevision = SceneRevision(3U),
    };
    auto nan = tracker.prepareApply(insertDelta(2U, 3U, 3U, authoritative, nanHint));
    EXPECT(context, nan.hasValue());
    if (nan) {
        tracker.commit(std::move(nan.value()));
    }
    EXPECT(context, tracker.diagnostics().rejectedHints == 2U);
}

void testViewsAreIndependentAndMonotonic(TestContext& context) {
    SceneViewRegistry views;
    auto zero = views.attach(0U, SceneRevision(0U));
    EXPECT(context, !zero.hasValue());
    EXPECT(context, zero.error().code == ErrorCode::kInvalidArgument);
    EXPECT(context, views.attach(1U, SceneRevision(2U)).hasValue());
    auto duplicate = views.attach(1U, SceneRevision(2U));
    EXPECT(context, !duplicate.hasValue());
    EXPECT(context, duplicate.error().code == ErrorCode::kDuplicateObject);
    EXPECT(context, views.attach(2U, SceneRevision(4U)).hasValue());
    EXPECT(context, views.minimumPresentedRevision() == SceneRevision(2U));
    auto backward = views.present(2U, SceneRevision(3U));
    EXPECT(context, !backward.hasValue());
    EXPECT(context, backward.error().code == ErrorCode::kInvalidRevision);
    EXPECT(context, views.present(1U, SceneRevision(5U)).hasValue());
    EXPECT(context, views.minimumPresentedRevision() == SceneRevision(4U));
    EXPECT(context, views.detach(1U));
    EXPECT(context, views.minimumPresentedRevision() == SceneRevision(4U));
    EXPECT(context, views.detach(2U));
    EXPECT(context, views.minimumPresentedRevision() == SceneRevision(0U));
}

} // namespace

int main() {
    TestContext context;
    testCollapseAndCompact(context);
    testRectAndByteLimits(context);
    testDefaultPublishedLimits(context);
    testHintsNeverShrinkAuthoritativeDamage(context);
    testViewsAreIndependentAndMonotonic(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " RF01-3 Damage/View expectations failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RF01-3 Damage/View tests passed\n";
    return EXIT_SUCCESS;
}
