#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/scene/damage_tracker.hpp"
#include "canvas/scene/render_scene.hpp"
#include "canvas/scene/scene_frame.hpp"
#include "canvas/scene/scene_query.hpp"
#include "canvas/scene/scene_record_store.hpp"
#include "canvas/scene/scene_types.hpp"
#include "canvas/scene/spatial_index.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace canvas {

struct SceneApplyReceipt final {
    SceneRevision beforeRevision;
    SceneRevision afterRevision;
    std::uint64_t recordsTouched = 0;
    std::uint64_t renderNodesTouched = 0;
    std::uint64_t spatialRecordsTouched = 0;
    DamageSet damage;
};

struct SceneCommitDiagnostics final {
    std::uint64_t transactionCount = 0;
    std::uint8_t recordStoreStage = 0;
    std::uint8_t renderSceneStage = 0;
    std::uint8_t spatialIndexStage = 0;
    std::uint8_t damageStage = 0;
    std::uint8_t revisionStage = 0;
};

class SceneReadView final {
  public:
    [[nodiscard]] SceneRevision revision() const {
        return _revision;
    }
    [[nodiscard]] std::span<const SceneRecord> records() const {
        return _records;
    }

    [[nodiscard]] const SceneRecord* find(ObjectId objectId) const;

  private:
    friend class Scene;

    SceneReadView(SceneRevision revision, std::span<const SceneRecord> records)
        : _revision(revision), _records(records) {}

    SceneRevision _revision;
    std::span<const SceneRecord> _records;
};

class Scene final {
  public:
    Scene(std::unique_ptr<IRenderScene> renderScene, std::unique_ptr<ISpatialIndex> spatialIndex);

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    [[nodiscard]] SceneRevision revision() const {
        return _revision;
    }
    [[nodiscard]] SceneReadView read() const {
        return SceneReadView(_revision, _records.records());
    }

    foundation::Result<SceneApplyReceipt> replace(CompiledSceneSnapshot snapshot);
    foundation::Result<SceneApplyReceipt> apply(CompiledSceneDelta delta);

    foundation::Result<SceneQueryResult> query(const SceneQuery& query) const;
    foundation::Result<HitTestResult> hitTest(const HitTestRequest& request) const;
    foundation::Result<SceneDrawList> buildDrawList(const SceneQueryResult& visible) const;
    foundation::Result<SceneFrameInput> buildFrame(const SceneQuery& query,
                                                    SceneRevision afterExclusive) const;

    [[nodiscard]] DamageSet collectDamage(SceneRevision afterExclusive,
                                          SceneRevision throughInclusive) const;
    void compactDamageThrough(SceneRevision revision);

    [[nodiscard]] SceneCommitDiagnostics commitDiagnostics() const {
        return _commitDiagnostics;
    }
    [[nodiscard]] DamageDiagnostics damageDiagnostics() const {
        return _damageTracker.diagnostics();
    }

  private:
    SceneRecordStore _records;
    std::unique_ptr<IRenderScene> _renderScene;
    std::unique_ptr<ISpatialIndex> _spatialIndex;
    DamageTracker _damageTracker;
    SceneRevision _revision;
    SceneCommitDiagnostics _commitDiagnostics;
};

} // namespace canvas
