#pragma once

#include "canvas/foundation/revision.hpp"
#include "canvas/foundation/result.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace canvas {

using foundation::SceneRevision;
using SceneViewId = std::uint64_t;

struct SceneViewState final {
    SceneViewId viewId = 0;
    SceneRevision presentedRevision;
};

class SceneViewRegistry final {
  public:
    foundation::Result<SceneViewState> attach(SceneViewId viewId,
                                               SceneRevision presentedRevision);
    foundation::Result<SceneViewState> present(SceneViewId viewId,
                                                SceneRevision presentedRevision);
    bool detach(SceneViewId viewId);

    foundation::Result<SceneViewState> state(SceneViewId viewId) const;
    [[nodiscard]] SceneRevision minimumPresentedRevision() const;
    [[nodiscard]] std::size_t liveViewCount() const {
        return _views.size();
    }

  private:
    std::unordered_map<SceneViewId, SceneRevision> _views;
};

} // namespace canvas
