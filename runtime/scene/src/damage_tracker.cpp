#include "canvas/scene/damage_tracker.hpp"

#include "canvas/foundation/world_geometry.hpp"

#include <algorithm>
#include <numeric>
#include <new>
#include <utility>

namespace canvas {
namespace {

constexpr std::size_t kEstimatedEntryBytes = 64U;
constexpr std::size_t kEstimatedRectBytes = 32U;

foundation::Error outOfMemoryError() {
    return foundation::Error{foundation::ErrorCode::kOutOfMemory,
                             "Unable to prepare damage journal"};
}

bool hasFlag(InvalidationHintFlags value, InvalidationHintFlags flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

bool hasOnlyKnownFlags(InvalidationHintFlags flags) {
    constexpr std::uint32_t knownFlags =
        static_cast<std::uint32_t>(InvalidationHintFlags::kLayoutChanged) |
        static_cast<std::uint32_t>(InvalidationHintFlags::kResourceChanged) |
        static_cast<std::uint32_t>(InvalidationHintFlags::kOrderChanged);
    return (static_cast<std::uint32_t>(flags) & ~knownFlags) == 0U;
}

bool contains(const WorldRect& outer, const WorldRect& inner) {
    return outer.isFiniteAndOrdered() && inner.isFiniteAndOrdered() &&
           outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right &&
           outer.bottom >= inner.bottom;
}

std::size_t estimateBytes(const std::vector<DamageSet>& journal) {
    std::size_t result = journal.size() * kEstimatedEntryBytes;
    for (const DamageSet& damage : journal) {
        result += damage.rects.size() * kEstimatedRectBytes;
    }
    return result;
}

void appendUnion(DamageSet& target, const DamageSet& source) {
    target.fullScene = target.fullScene || source.fullScene;
    for (const DamageRect& rect : source.rects) {
        if (target.rects.empty()) {
            target.rects.push_back(rect);
            target.rects.front().reasons = DamageReason::kFullRebuild;
        } else {
            target.rects.front().worldRect =
                foundation::unionRects(target.rects.front().worldRect, rect.worldRect);
            target.rects.front().reasons = DamageReason::kFullRebuild;
        }
    }
}

} // namespace

DamageTracker::DamageTracker(DamageLimits limits) : _limits(limits) {
    _limits.maxJournalEntries = std::max<std::size_t>(1U, _limits.maxJournalEntries);
    _limits.maxRectCount = std::max<std::size_t>(1U, _limits.maxRectCount);
    _limits.maxBytes = std::max<std::size_t>(kEstimatedEntryBytes + kEstimatedRectBytes,
                                             _limits.maxBytes);
}

foundation::Result<PreparedDamage> DamageTracker::prepareReplace(
    SceneRevision beforeRevision,
    SceneRevision afterRevision,
    const WorldRect& oldContentBounds,
    const WorldRect& newContentBounds) const {
    try {
        DamageSet damage{
            .afterExclusive = beforeRevision,
            .throughInclusive = afterRevision,
            .fullScene = true,
            .rects = {DamageRect{.worldRect = foundation::unionRects(oldContentBounds,
                                                                     newContentBounds),
                                 .reasons = DamageReason::kFullRebuild}},
        };
        return prepareAppend(std::move(damage));
    } catch (const std::bad_alloc&) {
        return foundation::Result<PreparedDamage>::failure(outOfMemoryError());
    }
}

foundation::Result<PreparedDamage>
DamageTracker::prepareApply(const CompiledSceneDelta& delta) const {
    try {
        DamageSet damage{.afterExclusive = delta.beforeRevision,
                         .throughInclusive = delta.afterRevision};
        damage.rects.reserve(delta.mutations.size());
        for (const SceneMutation& mutation : delta.mutations) {
            WorldRect dirty;
            DamageReason reasons = DamageReason::kContent;
            if (mutation.before && mutation.after) {
                dirty = foundation::unionRects(mutation.before->worldBounds,
                                               mutation.after->worldBounds);
                if (mutation.before->orderKey != mutation.after->orderKey) {
                    reasons = static_cast<DamageReason>(
                        static_cast<std::uint32_t>(reasons) |
                        static_cast<std::uint32_t>(DamageReason::kOrder));
                }
            } else if (mutation.before) {
                dirty = mutation.before->worldBounds;
            } else if (mutation.after) {
                dirty = mutation.after->worldBounds;
            }
            damage.rects.push_back(DamageRect{.worldRect = dirty, .reasons = reasons});
        }

        std::uint64_t rejectedHints = 0;
        if (delta.hints) {
            const InvalidationHints& hints = *delta.hints;
            const bool hasPartialStamp = hints.beforeRevision.has_value() !=
                                         hints.afterRevision.has_value();
            const bool validRect = !hints.worldDirty || hints.worldDirty->isFiniteAndOrdered();
            const bool stamped = !hasPartialStamp && hints.beforeRevision &&
                                 hints.afterRevision &&
                                 *hints.beforeRevision == delta.beforeRevision &&
                                 *hints.afterRevision == delta.afterRevision;
            bool accepted = stamped && validRect && hasOnlyKnownFlags(hints.flags);
            WorldRect authoritative;
            bool hasAuthoritative = false;
            for (const DamageRect& rect : damage.rects) {
                authoritative = hasAuthoritative
                                    ? foundation::unionRects(authoritative, rect.worldRect)
                                    : rect.worldRect;
                hasAuthoritative = true;
            }
            if (accepted && hints.worldDirty) {
                if (hasAuthoritative && !contains(*hints.worldDirty, authoritative)) {
                    accepted = false;
                }
            }
            if (accepted) {
                std::uint32_t reasonBits = static_cast<std::uint32_t>(DamageReason::kContent);
                reasonBits |= hasFlag(hints.flags, InvalidationHintFlags::kOrderChanged)
                                  ? static_cast<std::uint32_t>(DamageReason::kOrder)
                                  : 0U;
                reasonBits |= hasFlag(hints.flags, InvalidationHintFlags::kResourceChanged)
                                  ? static_cast<std::uint32_t>(DamageReason::kResource)
                                  : 0U;
                reasonBits |= hasFlag(hints.flags, InvalidationHintFlags::kLayoutChanged)
                                  ? static_cast<std::uint32_t>(DamageReason::kLayout)
                                  : 0U;
                for (DamageRect& rect : damage.rects) {
                    rect.reasons = static_cast<DamageReason>(
                        static_cast<std::uint32_t>(rect.reasons) | reasonBits);
                }
                if (hints.worldDirty && hasAuthoritative &&
                    !contains(authoritative, *hints.worldDirty)) {
                    damage.rects.push_back(DamageRect{
                        .worldRect = *hints.worldDirty,
                        .reasons = static_cast<DamageReason>(reasonBits),
                    });
                }
            }
            if (!accepted) {
                ++rejectedHints;
            }
        }

        auto result = prepareAppend(std::move(damage));
        if (result) {
            result.value()._rejectedHints += rejectedHints;
        }
        return result;
    } catch (const std::bad_alloc&) {
        return foundation::Result<PreparedDamage>::failure(outOfMemoryError());
    }
}

foundation::Result<PreparedDamage> DamageTracker::prepareAppend(DamageSet damage) const {
    try {
        std::vector<DamageSet> journal = _journal;
        journal.push_back(std::move(damage));
        std::uint64_t collapseCount = _collapseCount;
        const std::size_t rectCount = std::accumulate(
            journal.begin(), journal.end(), std::size_t{0}, [](std::size_t count,
                                                               const DamageSet& item) {
                return count + item.rects.size();
            });
        if (journal.size() > _limits.maxJournalEntries || rectCount > _limits.maxRectCount ||
            estimateBytes(journal) > _limits.maxBytes) {
            DamageSet collapsed{.afterExclusive = _compactedThrough,
                                .throughInclusive = journal.back().throughInclusive,
                                .fullScene = true};
            for (const DamageSet& item : journal) {
                collapsed.throughInclusive =
                    std::max(collapsed.throughInclusive, item.throughInclusive);
                appendUnion(collapsed, item);
            }
            journal.clear();
            journal.push_back(std::move(collapsed));
            ++collapseCount;
        }
        return foundation::Result<PreparedDamage>::success(
            PreparedDamage(std::move(journal), collapseCount, _rejectedHints));
    } catch (const std::bad_alloc&) {
        return foundation::Result<PreparedDamage>::failure(outOfMemoryError());
    }
}

void DamageTracker::commit(PreparedDamage damage) noexcept {
    _journal.swap(damage._journal);
    _collapseCount = damage._collapseCount;
    _rejectedHints = damage._rejectedHints;
}

DamageSet DamageTracker::collect(SceneRevision afterExclusive,
                                 SceneRevision throughInclusive) const {
    DamageSet result{.afterExclusive = afterExclusive,
                     .throughInclusive = throughInclusive};
    if (afterExclusive < _compactedThrough) {
        result.fullScene = true;
        return result;
    }
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
    if (minimumPresentedRevision <= _compactedThrough) {
        return;
    }
    _compactedThrough = minimumPresentedRevision;
    std::erase_if(_journal, [minimumPresentedRevision](const DamageSet& damage) {
        return damage.throughInclusive <= minimumPresentedRevision;
    });
}

DamageDiagnostics DamageTracker::diagnostics() const {
    std::size_t rectCount = 0;
    for (const DamageSet& damage : _journal) {
        rectCount += damage.rects.size();
    }
    return DamageDiagnostics{.compactedThrough = _compactedThrough,
                             .journalEntries = _journal.size(),
                             .rectCount = rectCount,
                             .estimatedBytes = estimateBytes(_journal),
                             .collapseCount = _collapseCount,
                             .rejectedHints = _rejectedHints};
}

} // namespace canvas
