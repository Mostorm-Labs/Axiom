#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

int main() {
  skia::textlayout::ParagraphStyle style;
  auto fonts = sk_make_sp<skia::textlayout::FontCollection>();
  auto unicode = SkUnicodes::ICU::Make();
  auto paragraph = skia::textlayout::ParagraphBuilder::make(style, fonts, unicode);
  return paragraph ? 0 : 1;
}
