#pragma once

#include "canvas/scene/damage_tracker.hpp"
#include "canvas/scene/render_scene.hpp"
#include "canvas/scene/scene_query.hpp"

namespace canvas {

struct SceneFrameInput final {
    SceneRevision revision;
    DamageSet damage;
    SceneQueryResult query;
    SceneDrawList drawList;
};

} // namespace canvas
