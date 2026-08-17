#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

int main() {
  auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(8, 8));
  return surface ? 0 : 1;
}
