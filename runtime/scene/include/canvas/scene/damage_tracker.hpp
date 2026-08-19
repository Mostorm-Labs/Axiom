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

class PreparedDamage final {
  public:
    PreparedDamage(PreparedDamage&&) noexcept = default;
    PreparedDamage& operator=(PreparedDamage&&) noexcept = default;

    PreparedDamage(const PreparedDamage&) = delete;
    PreparedDamage& operator=(const PreparedDamage&) = delete;

  private:
    friend class DamageTracker;

    explicit PreparedDamage(std::vector<DamageSet> journal) : _journal(std::move(journal)) {}

    std::vector<DamageSet> _journal;
};

class DamageTracker final {
  public:
    foundation::Result<PreparedDamage> prepareReplace(SceneRevision beforeRevision,
                                                      SceneRevision afterRevision,
                                                      const WorldRect& oldContentBounds,
                                                      const WorldRect& newContentBounds) const;

    foundation::Result<PreparedDamage> prepareApply(const CompiledSceneDelta& delta) const;

    void commit(PreparedDamage damage) noexcept;

    [[nodiscard]] DamageSet collect(SceneRevision afterExclusive,
                                    SceneRevision throughInclusive) const;
    void compactThrough(SceneRevision minimumPresentedRevision);
    [[nodiscard]] std::size_t journalSize() const {
        return _journal.size();
    }

  private:
    std::vector<DamageSet> _journal;
};

} // namespace canvas
