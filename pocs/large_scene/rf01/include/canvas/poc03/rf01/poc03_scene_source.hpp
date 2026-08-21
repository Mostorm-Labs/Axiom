#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/poc03/large_scene.h"
#include "canvas/scene/scene.hpp"
#include "canvas/scene/scene_compiler.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace canvas::poc03::rf01 {

class Poc03SceneSource final : public ICompiledSceneSource {
  public:
    Poc03SceneSource() = default;
    Poc03SceneSource(const Document& document, const ChangeSet* changes = nullptr)
        : _document(&document), _changes(changes) {}

    void setChangeSet(const ChangeSet* changes) {
        _changes = changes;
    }

    foundation::Result<CompiledSceneSnapshot> compileFull() const override;
    foundation::Result<CompiledSceneDelta> compileDelta() const override;

    foundation::Result<CompiledSceneSnapshot> compileFull(const Document& document) const;
    foundation::Result<CompiledSceneDelta> compileDelta(const Document& document,
                                                        const ChangeSet& changes) const;

    foundation::Result<std::string> projectedDigest(SceneReadView scene,
                                                    const Document& document) const;

    foundation::Result<std::vector<std::uint8_t>> referenceRgba(const RuntimeScene& scene,
                                                                const ViewState& view) const;

    foundation::Result<std::vector<std::uint8_t>>
    referenceRgba(const Scene& scene, const Document& document, const ViewState& view) const;

  private:
    foundation::Result<std::uint32_t> slotFor(std::uint64_t objectId) const;

    const Document* _document = nullptr;
    const ChangeSet* _changes = nullptr;
    mutable std::unordered_map<std::uint64_t, std::uint32_t> _slots;
    mutable std::uint64_t _nextSlot = 0;
};

} // namespace canvas::poc03::rf01
