#include "canvas/scene/damage_tracker.hpp"

#include "canvas/foundation/world_geometry.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace canvas {
namespace {

foundation::Error outOfMemoryError() {
    return foundation::Error{
        foundation::ErrorCode::kOutOfMemory,
        "Unable to prepare damage journal",
    };
}

} // namespace

foundation::Result<PreparedDamage>
DamageTracker::prepareReplace(SceneRevision beforeRevision,
                              SceneRevision afterRevision,
                              const WorldRect& oldContentBounds,
                              const WorldRect& newContentBounds) const {
    try {
        std::vector<DamageSet> journal = _journal;
        DamageSet damage{
            .afterExclusive = beforeRevision,
            .throughInclusive = afterRevision,
            .fullScene = true,
        };
        damage.rects.push_back(DamageRect{
            .worldRect = foundation::unionRects(oldContentBounds, newContentBounds),
            .reasons = DamageReason::kFullRebuild,
        });
        journal.push_back(std::move(damage));
        return foundation::Result<PreparedDamage>::success(PreparedDamage(std::move(journal)));
    } catch (const std::bad_alloc&) {
        return foundation::Result<PreparedDamage>::failure(outOfMemoryError());
    }
}

foundation::Result<PreparedDamage>
DamageTracker::prepareApply(const CompiledSceneDelta& delta) const {
    try {
        std::vector<DamageSet> journal = _journal;
        DamageSet damage{
            .afterExclusive = delta.beforeRevision,
            .throughInclusive = delta.afterRevision,
        };
        damage.rects.reserve(delta.mutations.size());
        for (const SceneMutation& mutation : delta.mutations) {
            WorldRect dirty;
            if (mutation.before && mutation.after) {
                dirty = foundation::unionRects(mutation.before->worldBounds,
                                               mutation.after->worldBounds);
            } else if (mutation.before) {
                dirty = mutation.before->worldBounds;
            } else if (mutation.after) {
                dirty = mutation.after->worldBounds;
            }
            damage.rects.push_back(DamageRect{
                .worldRect = dirty,
                .reasons = DamageReason::kContent,
            });
        }
        journal.push_back(std::move(damage));
        return foundation::Result<PreparedDamage>::success(PreparedDamage(std::move(journal)));
    } catch (const std::bad_alloc&) {
        return foundation::Result<PreparedDamage>::failure(outOfMemoryError());
    }
}

void DamageTracker::commit(PreparedDamage damage) noexcept {
    _journal.swap(damage._journal);
}

DamageSet DamageTracker::collect(SceneRevision afterExclusive,
                                 SceneRevision throughInclusive) const {
    DamageSet result{
        .afterExclusive = afterExclusive,
        .throughInclusive = throughInclusive,
    };
    for (const DamageSet& damage : _journal) {
        if (damage.throughInclusive <= afterExclusive ||
            damage.afterExclusive >= throughInclusive) {
            continue;
        }
        result.fullScene = result.fullScene || damage.fullScene;
        result.rects.insert(result.rects.end(), damage.rects.begin(), damage.rects.end());
    }
    return result;
}

void DamageTracker::compactThrough(SceneRevision minimumPresentedRevision) {
    std::erase_if(_journal, [minimumPresentedRevision](const DamageSet& damage) {
        return damage.throughInclusive <= minimumPresentedRevision;
    });
}

} // namespace canvas
