#include "skparagraph_layout.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>

#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_data.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/Metrics.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

namespace canvas::poc04 {
namespace {
using skia::textlayout::FontCollection;
using skia::textlayout::LineMetrics;
using skia::textlayout::ParagraphBuilder;
using skia::textlayout::ParagraphStyle;
using SkTextStyle = skia::textlayout::TextStyle;

uint64_t FlatOffset(const TextDocument& document, LogicalPosition position) {
  return document.FlatUtf16Offset(position);
}

LogicalPosition FromFlat(const TextDocument& document, uint64_t offset) {
  return document.PositionAtFlatUtf16Offset(offset);
}

uint64_t Utf8OffsetToUtf16(std::string_view utf8, uint64_t byte_offset) {
  if (byte_offset > utf8.size()) {
    throw std::out_of_range("SkParagraph UTF-8 offset is outside text");
  }
  // SkParagraph line metrics are UTF-8 byte offsets, but a line boundary can
  // be reported in the middle of a multibyte scalar (notably at a hard break
  // immediately after CJK text).  The semantic document boundary is UTF-16;
  // clamp such an index to the beginning of that scalar instead of asking the
  // strict decoder to parse a truncated byte sequence.
  while (byte_offset > 0 && byte_offset < utf8.size() &&
         (static_cast<unsigned char>(utf8[byte_offset]) & 0xc0U) == 0x80U) {
    --byte_offset;
  }
  return Utf8ToUtf16(utf8.substr(0, byte_offset)).size();
}

const char* FamilyForResource(std::string_view resource_id) {
  if (resource_id == kRobotoRegularResourceId) return "Roboto";
  if (resource_id == kNotoSansCjkSubsetResourceId) return "Noto Sans CJK SC";
  return nullptr;
}

std::vector<uint8_t> LoadBytes(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                   {});
  if (bytes.empty()) {
    throw std::runtime_error("canonical font fixture is missing: " + path);
  }
  return bytes;
}

std::shared_ptr<FontResourceResolver> LoadCanonicalFonts(
    const std::string& latin_font_path, const std::string& cjk_font_path) {
  auto resolver = std::make_shared<FontResourceResolver>();
  const std::vector<uint8_t> latin = LoadBytes(latin_font_path);
  if (!resolver->Register(std::string(kRobotoRegularResourceId),
                          std::string(kRobotoRegularSha256), latin)) {
    throw std::runtime_error("canonical Roboto fixture failed verification: " +
                             resolver->LastDiagnostic());
  }
  const std::vector<uint8_t> cjk = LoadBytes(cjk_font_path);
  if (!resolver->Register(std::string(kNotoSansCjkSubsetResourceId),
                          std::string(kNotoSansCjkSubsetSha256), cjk)) {
    throw std::runtime_error("canonical CJK fixture failed verification: " +
                             resolver->LastDiagnostic());
  }
  return resolver;
}
}  // namespace

SkParagraphTextLayout::SkParagraphTextLayout(std::string latin_font_path,
                                             std::string cjk_font_path)
    : font_resolver_(LoadCanonicalFonts(latin_font_path, cjk_font_path)) {
  const FontResolution latin = font_resolver_->Resolve(
      kRobotoRegularResourceId, {});
  const FontResolution cjk = font_resolver_->Resolve(
      kNotoSansCjkSubsetResourceId, {});
  if (latin.diagnostic != FontDiagnostic::kOk ||
      cjk.diagnostic != FontDiagnostic::kOk) {
    throw std::runtime_error("canonical font resources are unavailable");
  }
  std::array<sk_sp<SkData>, 2> fonts = {
      SkData::MakeWithCopy(latin.bytes.data(), latin.bytes.size()),
      SkData::MakeWithCopy(cjk.bytes.data(), cjk.bytes.size())};
  font_manager_ = SkFontMgr_New_Custom_Data(
      SkSpan<sk_sp<SkData>>(fonts.data(), fonts.size()));
  if (!font_manager_) {
    throw std::runtime_error("canonical font manager could not initialize");
  }
  font_collection_ = sk_make_sp<FontCollection>();
  font_collection_->setAssetFontManager(font_manager_);
  font_collection_->setDefaultFontManager(font_manager_, "Roboto");
  font_collection_->disableFontFallback();
  unicode_ = SkUnicodes::ICU::Make();
  if (!unicode_) throw std::runtime_error("bundled ICU could not initialize");
}

std::unique_ptr<skia::textlayout::Paragraph>
SkParagraphTextLayout::BuildParagraph(const TextDocument& document) {
  ParagraphStyle paragraph_style;
  paragraph_style.turnHintingOff();
  SkTextStyle default_style;
  default_style.setFontFamilies({SkString("Roboto")});
  default_style.setFontSize(16.0F);
  paragraph_style.setTextStyle(default_style);
  auto builder = ParagraphBuilder::make(paragraph_style, font_collection_,
                                        unicode_);
  for (size_t paragraph_index = 0; paragraph_index < document.paragraphs().size(); ++paragraph_index) {
    const Paragraph& paragraph = document.paragraphs()[paragraph_index];
    for (const TextRun& run : paragraph.runs) {
      if (run.style.font_resource_id != kRobotoRegularResourceId ||
          run.style.font_content_hash != kRobotoRegularSha256 ||
          run.style.fallback_chain != std::vector<FontResourceReference>{{
              std::string(kNotoSansCjkSubsetResourceId),
              std::string(kNotoSansCjkSubsetSha256)}}) {
        throw std::runtime_error("canonical font identity or fallback chain is unavailable");
      }
      SkTextStyle style = default_style;
      style.setFontSize(run.style.font_size);
      style.setColor(run.style.rgba);
      style.setFontStyle(SkFontStyle(
          run.style.weight, SkFontStyle::kNormal_Width,
          run.style.italic ? SkFontStyle::kItalic_Slant
                           : SkFontStyle::kUpright_Slant));
      style.setLocale(SkString(run.style.locale));
      std::vector<SkString> families;
      const char* primary_family = FamilyForResource(run.style.font_resource_id);
      if (!primary_family) throw std::runtime_error("canonical font resource is unknown");
      families.emplace_back(primary_family);
      for (const auto& fallback : run.style.fallback_chain) {
        const char* family = FamilyForResource(fallback.resource_id);
        if (!family) throw std::runtime_error("canonical fallback resource is unknown");
        families.emplace_back(family);
      }
      style.setFontFamilies(std::move(families));
      builder->pushStyle(style);
      builder->addText(run.text);
      builder->pop();
    }
    if (paragraph_index + 1 < document.paragraphs().size()) builder->addText(u"\n");
  }
  return builder->Build();
}

TextLayoutResult SkParagraphTextLayout::Layout(const TextDocument& document,
                                               float width,
                                               TextRange selection) {
  if (!std::isfinite(width) || width <= 0.0F ||
      !document.IsValidPosition(selection.anchor) ||
      !document.IsValidPosition(selection.focus)) {
    throw std::invalid_argument("layout width and selection must be valid");
  }

  auto paragraph = BuildParagraph(document);
  paragraph->layout(width);
  const std::u16string plain = document.PlainText();
  const std::string utf8 = Utf16ToUtf8(plain);
  TextLayoutResult result;
  result.width = width;
  result.height = paragraph->getHeight();
  std::vector<LineMetrics> metrics;
  paragraph->getLineMetrics(metrics);
  for (const LineMetrics& line : metrics) {
    result.lines.push_back(
        {FromFlat(document, Utf8OffsetToUtf16(utf8, line.fStartIndex)),
         FromFlat(document, Utf8OffsetToUtf16(utf8, line.fEndIndex)),
         static_cast<float>(line.fBaseline),
         static_cast<float>(line.fWidth),
         static_cast<float>(line.fHeight)});
  }
  size_t last_cluster_end = 0;
  for (size_t offset = 0; offset < plain.size(); ++offset) {
    skia::textlayout::Paragraph::GlyphInfo info;
    if (!paragraph->getGlyphInfoAtUTF16Offset(offset, &info) ||
        info.fGraphemeClusterTextRange.end <= last_cluster_end) {
      continue;
    }
    last_cluster_end = info.fGraphemeClusterTextRange.end;
    const SkRect& bounds = info.fGraphemeLayoutBounds;
    result.clusters.push_back({
        {FromFlat(document, info.fGraphemeClusterTextRange.start),
         FromFlat(document, info.fGraphemeClusterTextRange.end)},
        {bounds.fLeft, bounds.fTop, bounds.width(), bounds.height()}});
  }
  if (paragraph->unresolvedGlyphs() > 0) {
    result.diagnostics.push_back("unresolved-glyphs");
  }

  const TextRange normalized = selection.Normalized();
  const auto boxes = paragraph->getRectsForRange(
      FlatOffset(document, normalized.anchor), FlatOffset(document, normalized.focus),
      skia::textlayout::RectHeightStyle::kTight,
      skia::textlayout::RectWidthStyle::kTight);
  for (const auto& box : boxes) {
    result.selection_rects.push_back({box.rect.fLeft, box.rect.fTop,
                                      box.rect.width(), box.rect.height()});
  }
  return result;
}

TextLayoutResult SkParagraphTextLayout::LayoutForPerformance(
    const TextDocument& document, float width) {
  if (!std::isfinite(width) || width <= 0.0F) {
    throw std::invalid_argument("layout width must be finite and positive");
  }

  // Deliberately build a new paragraph for every invocation. This is a
  // canonical layout benchmark, not a cache-hit benchmark. Layout() remains
  // the geometry oracle for the small fixed fixture; avoiding its per-offset
  // diagnostic queries here keeps the 10K gate representative of shaping and
  // line breaking work.
  auto paragraph = BuildParagraph(document);
  paragraph->layout(width);
  TextLayoutResult result;
  result.width = width;
  result.height = paragraph->getHeight();
  if (paragraph->unresolvedGlyphs() > 0) {
    result.diagnostics.push_back("unresolved-glyphs");
  }
  return result;
}

}  // namespace canvas::poc04
