#ifndef CANVAS_POC03_SKIA_LARGE_SCENE_RENDERER_H_
#define CANVAS_POC03_SKIA_LARGE_SCENE_RENDERER_H_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "canvas/poc03/large_scene.h"
#if defined(CANVAS_POC03_INK_INTEGRATION)
#include "canvas/poc03/ink_integration.h"
#endif

class SkCanvas;
class SkSurface;

namespace canvas::poc03 {

void DrawLargeScene(SkCanvas& canvas, const RuntimeScene& scene,
                    const ViewState& view, const FrameGraph& frame
#if defined(CANVAS_POC03_INK_INTEGRATION)
                    , const InkGeometryStore* ink_geometry = nullptr,
                    const poc02::DefaultPreviewSink::State* preview = nullptr
#endif
                    );
bool ReadRgba(SkSurface& surface, uint32_t width, uint32_t height,
              std::vector<uint8_t>* rgba);
std::string PixelDigest(std::span<const uint8_t> rgba);

}  // namespace canvas::poc03

#endif
