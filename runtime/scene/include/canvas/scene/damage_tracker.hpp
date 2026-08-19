#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/scene/scene_types.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace canvas {

enum class DamageReason : std::uint32_t {
    kContent = 1U << 0U,
    kOrder = 1U << 1U,
    kResource = 1U << 2U,
    kLayout = 1U << 3U,
    kFullRebuild = 1U << 4U,
};

struct DamageRect final {
    WorldRect worldRect;
    DamageReason reasons = DamageReason::kContent;
};

struct DamageSet final {
    SceneRevision afterExclusive;
    SceneRevision throughInclusive;
    bool fullScene = false;
    std::vector<DamageRect> rects;
};

struct DamageLimits final {
    std::size_t maxJournalEntries = 256U;
    std::size_t maxRectCount = 4096U;
    std::size_t maxBytes = 1024U * 1024U;
};

struct DamageDiagnostics final {
    SceneRevision compactedThrough;
    std::size_t journalEntries = 0;
    std::size_t rectCount = 0;
    std::size_t estimatedBytes = 0;
    std::uint64_t collapseCount = 0;
    std::uint64_t rejectedHints = 0;
};

class PreparedDamage final {
  public:
    PreparedDamage(PreparedDamage&&) noexcept = default;
    PreparedDamage& operator=(PreparedDamage&&) noexcept = default;

    PreparedDamage(const PreparedDamage&) = delete;
    PreparedDamage& operator=(const PreparedDamage&) = delete;

  private:
    friend class DamageTracker;

    PreparedDamage(std::vector<DamageSet> journal,
                   std::uint64_t collapseCount,
                   std::uint64_t rejectedHints)
        : _journal(std::move(journal)),
          _collapseCount(collapseCount),
          _rejectedHints(rejectedHints) {}

    std::vector<DamageSet> _journal;
    std::uint64_t _collapseCount = 0;
    std::uint64_t _rejectedHints = 0;
};

class DamageTracker final {
  public:
    explicit DamageTracker(DamageLimits limits = {});

    foundation::Result<PreparedDamage> prepareReplace(SceneRevision beforeRevision,
                                                      SceneRevision afterRevision,
                                                      const WorldRect& oldContentBounds,
                                                      const WorldRect& newContentBounds) const;

    foundation::Result<PreparedDamage> prepareApply(const CompiledSceneDelta& delta) const;

    void commit(PreparedDamage damage) noexcept;

    [[nodiscard]] DamageSet collect(SceneRevision afterExclusive,
                                    SceneRevision throughInclusive) const;
    void compactThrough(SceneRevision minimumPresentedRevision);
    [[nodiscard]] DamageDiagnostics diagnostics() const;
    [[nodiscard]] std::size_t journalSize() const {
        return _journal.size();
    }

  private:
    foundation::Result<PreparedDamage> prepareAppend(DamageSet damage) const;

    DamageLimits _limits;
    std::vector<DamageSet> _journal;
    SceneRevision _compactedThrough;
    std::uint64_t _collapseCount = 0;
    std::uint64_t _rejectedHints = 0;
};

} // namespace canvas
