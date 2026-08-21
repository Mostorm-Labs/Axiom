#include "canvas/scene/scene_view.hpp"

#include <limits>
#include <new>

namespace canvas {
namespace {

foundation::Error makeError(foundation::ErrorCode code, const char* message) {
    return foundation::Error{code, message};
}

} // namespace

foundation::Result<SceneViewState> SceneViewRegistry::attach(SceneViewId viewId,
                                                             SceneRevision presentedRevision) {
    if (viewId == 0U) {
        return foundation::Result<SceneViewState>::failure(
            makeError(foundation::ErrorCode::kInvalidArgument, "SceneViewId must not be zero"));
    }
    if (_views.contains(viewId)) {
        return foundation::Result<SceneViewState>::failure(
            makeError(foundation::ErrorCode::kDuplicateObject, "SceneViewId is already attached"));
    }
    try {
        _views.emplace(viewId, presentedRevision);
        return foundation::Result<SceneViewState>::success(
            SceneViewState{viewId, presentedRevision});
    } catch (const std::bad_alloc&) {
        return foundation::Result<SceneViewState>::failure(
            makeError(foundation::ErrorCode::kOutOfMemory, "Unable to attach SceneView"));
    }
}

foundation::Result<SceneViewState> SceneViewRegistry::present(SceneViewId viewId,
                                                              SceneRevision presentedRevision) {
    const auto found = _views.find(viewId);
    if (found == _views.end()) {
        return foundation::Result<SceneViewState>::failure(
            makeError(foundation::ErrorCode::kMissingObject, "SceneViewId is not attached"));
    }
    if (presentedRevision < found->second) {
        return foundation::Result<SceneViewState>::failure(makeError(
            foundation::ErrorCode::kInvalidRevision,
            "Presented Scene revision cannot move backwards"));
    }
    found->second = presentedRevision;
    return foundation::Result<SceneViewState>::success(
        SceneViewState{viewId, presentedRevision});
}

bool SceneViewRegistry::detach(SceneViewId viewId) {
    return _views.erase(viewId) != 0U;
}

foundation::Result<SceneViewState> SceneViewRegistry::state(SceneViewId viewId) const {
    const auto found = _views.find(viewId);
    if (found == _views.end()) {
        return foundation::Result<SceneViewState>::failure(
            makeError(foundation::ErrorCode::kMissingObject, "SceneViewId is not attached"));
    }
    return foundation::Result<SceneViewState>::success(SceneViewState{viewId, found->second});
}

SceneRevision SceneViewRegistry::minimumPresentedRevision() const {
    SceneRevision result(std::numeric_limits<std::uint64_t>::max());
    if (_views.empty()) {
        return SceneRevision{};
    }
    for (const auto& [viewId, revision] : _views) {
        static_cast<void>(viewId);
        if (revision < result) {
            result = revision;
        }
    }
    return result;
}

} // namespace canvas
