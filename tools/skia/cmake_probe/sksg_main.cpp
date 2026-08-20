#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "modules/sksg/include/SkSGGroup.h"
#include "modules/sksg/include/SkSGScene.h"

int main() {
  const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(8, 8));
  const auto root = sksg::Group::Make();
  const auto scene = sksg::Scene::Make(root);
  if (!surface || !scene) {
    return 1;
  }
  scene->revalidate();
  scene->render(surface->getCanvas());
  return 0;
}
