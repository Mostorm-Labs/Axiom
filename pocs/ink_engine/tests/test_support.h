#ifndef CANVAS_POC02_TEST_SUPPORT_H_
#define CANVAS_POC02_TEST_SUPPORT_H_

#include <string>
#include <vector>

#include "canvas_poc02/ink_engine.h"

namespace canvas::poc02::test {

PointerSample Sample(uint64_t sequence, float x, float y, float pressure,
                     uint64_t timestamp_us,
                     PointerPhase phase = PointerPhase::kMove);
PointerSampleBatch Batch(std::vector<PointerSample> samples,
                         AffineTransform transform = {},
                         uint64_t viewport_revision = 1);
BrushDescriptor VectorBrush();
BrushDescriptor DabBrush();
std::string ReadFixture(std::string_view name);
AddStrokeOperation RunFixture(std::string_view name, StrokeDocument* document,
                              DefaultPreviewSink* sink);

}  // namespace canvas::poc02::test

#endif  // CANVAS_POC02_TEST_SUPPORT_H_
