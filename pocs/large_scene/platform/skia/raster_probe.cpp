#include <cstdlib>
#include <iostream>
#include <vector>

#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "skia_large_scene_renderer.h"

int main() {
  using namespace canvas::poc03;
  Document document = GenerateDocument({100000U, 0x43414e5641533033ULL,
                                         1000U, 32.0F});
  SceneCompiler compiler;
  RuntimeScene scene = compiler.CompileFull(document);
  NodeRecord changed = *document.Find(1U);
  changed.rgba ^= 0x00010101U;
  ++changed.content_revision;
  ChangeSet changes;
  CompileDiagnostics diagnostics;
  std::string error;
  if (!document.Apply({OperationKind::kUpdate, 1U, changed}, &changes, &error) ||
      !compiler.ApplyIncremental(document, changes, &scene, &diagnostics,
                                 &error)) {
    return EXIT_FAILURE;
  }
  const RuntimeScene oracle = compiler.CompileFull(document);
  const ViewState view{1U, 1U, 1U, Bounds{0.0F, 0.0F, 1280.0F, 720.0F},
                       1.0F, 1.0F, 1280U, 720U};
  const ViewQueryResult query = QueryView(scene, view, std::nullopt);
  const FrameGraph frame = BuildFrame(scene, query, {});
  const SkImageInfo info = SkImageInfo::Make(
      1280, 720, kRGBA_8888_SkColorType, kPremul_SkAlphaType,
      SkColorSpace::MakeSRGB());
  sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
  if (!surface) {
    return EXIT_FAILURE;
  }
  DrawLargeScene(*surface->getCanvas(), scene, view, frame);
  std::vector<uint8_t> rgba;
  if (!ReadRgba(*surface, 1280U, 720U, &rgba)) {
    return EXIT_FAILURE;
  }
  const std::string pixel_digest = PixelDigest(rgba);
  const ViewQueryResult oracle_query = QueryView(oracle, view, std::nullopt);
  DrawLargeScene(*surface->getCanvas(), oracle, view,
                 BuildFrame(oracle, oracle_query, {}));
  if (!ReadRgba(*surface, 1280U, 720U, &rgba)) {
    return EXIT_FAILURE;
  }
  const bool visual_equivalent = pixel_digest == PixelDigest(rgba);
  std::cout << "{\"backend\":\"skia-raster\",\"scene_digest\":\""
            << scene.Digest() << "\",\"pixel_digest\":\""
            << pixel_digest << "\",\"visual_equivalent\":"
            << (visual_equivalent ? "true" : "false")
            << ",\"candidates\":" << query.candidates.size() << "}\n";
  return visual_equivalent && scene.Digest() == oracle.Digest()
             ? EXIT_SUCCESS : EXIT_FAILURE;
}
