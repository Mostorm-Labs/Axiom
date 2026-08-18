#ifndef CANVAS_POC04_SKPARAGRAPH_LAYOUT_H_
#define CANVAS_POC04_SKPARAGRAPH_LAYOUT_H_

#include <memory>
#include <string>

#include "include/core/SkFontMgr.h"
#include "include/core/SkRefCnt.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skunicode/include/SkUnicode.h"

#include "canvas_poc04/rich_text.h"

namespace canvas::poc04 {

class SkParagraphTextLayout final : public TextLayout {
 public:
  SkParagraphTextLayout(std::string latin_font_path,
                        std::string cjk_font_path);
  TextLayoutResult Layout(const TextDocument& document, float width,
                          TextRange selection) override;

 private:
  std::shared_ptr<FontResourceResolver> font_resolver_;
  sk_sp<SkFontMgr> font_manager_;
  sk_sp<skia::textlayout::FontCollection> font_collection_;
  sk_sp<SkUnicode> unicode_;
};

}  // namespace canvas::poc04

#endif
