#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/poc03/large_scene.h"
#include "canvas/scene/scene.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace canvas::poc03::rf01 {

class Poc03SceneSource final {
  public:
    foundation::Result<CompiledSceneSnapshot> compileFull(const Document& document) const;

    foundation::Result<std::string> projectedDigest(SceneReadView scene,
                                                    const Document& document) const;

    foundation::Result<std::vector<std::uint8_t>> referenceRgba(const RuntimeScene& scene,
                                                                const ViewState& view) const;

    foundation::Result<std::vector<std::uint8_t>>
    referenceRgba(const Scene& scene, const Document& document, const ViewState& view) const;
};

} // namespace canvas::poc03::rf01
