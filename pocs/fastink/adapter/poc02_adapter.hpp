#ifndef CANVAS_POC06_POC02_ADAPTER_HPP_
#define CANVAS_POC06_POC02_ADAPTER_HPP_

#include <memory>

#include "arc/arc.hpp"

namespace canvas::poc02 {
class PreviewSink;
}

namespace canvas::poc06 {

std::unique_ptr<canvas::poc02::PreviewSink> CreateArcPreviewAdapter(
    arc::Bridge& bridge, uint64_t target_generation = 1);

}  // namespace canvas::poc06

#endif  // CANVAS_POC06_POC02_ADAPTER_HPP_
