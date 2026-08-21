#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkWebpEncoder.h"

int main() {
  unsigned char pixels[4] = {0, 0, 0, 255};
  const auto info = SkImageInfo::MakeN32Premul(1, 1);
  const SkPixmap pixmap(info, pixels, sizeof(pixels));
  SkDynamicMemoryWStream jpeg;
  SkDynamicMemoryWStream webp;
  return SkJpegEncoder::Encode(&jpeg, pixmap, {}) &&
                 SkWebpEncoder::Encode(&webp, pixmap, {})
             ? 0
             : 1;
}
