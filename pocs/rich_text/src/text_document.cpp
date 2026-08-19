#include "canvas_poc04/rich_text.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "foundation.h"

namespace canvas::poc04 {
namespace {

using Json = nlohmann::json;

void ValidateStyle(const TextStyle& style) {
  if (style.font_resource_id.empty() || !std::isfinite(style.font_size) ||
      style.font_size <= 0.0F || style.weight < 1 || style.weight > 1000 ||
      style.locale.empty() || style.font_content_hash.size() != 64 ||
      !std::ranges::all_of(style.font_content_hash, [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      }) ||
      !std::ranges::all_of(style.fallback_chain, [](const auto& fallback) {
        return !fallback.resource_id.empty() && fallback.content_hash.size() == 64 &&
               std::ranges::all_of(fallback.content_hash, [](char character) {
                 return (character >= '0' && character <= '9') ||
                        (character >= 'a' && character <= 'f');
               });
      })) {
    throw std::invalid_argument("invalid text style");
  }
}

void EncodeStyle(CanonicalEncoder& encoder, const TextStyle& style) {
  ValidateStyle(style);
  encoder.String(style.font_resource_id);
  encoder.String(style.font_content_hash);
  encoder.U64(style.fallback_chain.size());
  for (const auto& fallback : style.fallback_chain) {
    encoder.String(fallback.resource_id);
    encoder.String(fallback.content_hash);
  }
  encoder.F32(style.font_size);
  encoder.U32(style.rgba);
  encoder.U16(style.weight);
  encoder.U8(style.italic ? 1 : 0);
  encoder.String(style.locale);
  encoder.U64(style.attributes.size());
  for (const auto& [key, value] : style.attributes) {
    encoder.String(key);
    encoder.String(value);
  }
}

Json StyleToJson(const TextStyle& style) {
  Json fallback_chain = Json::array();
  for (const auto& fallback : style.fallback_chain) {
    fallback_chain.push_back({{"resource_id", fallback.resource_id},
                              {"content_hash", fallback.content_hash}});
  }
  return Json{{"font_resource_id", style.font_resource_id},
              {"font_content_hash", style.font_content_hash},
              {"fallback_chain", fallback_chain},
              {"font_size", style.font_size},
              {"rgba", style.rgba},
              {"weight", style.weight},
              {"italic", style.italic},
              {"locale", style.locale},
              {"attributes", style.attributes}};
}

TextStyle StyleFromJson(const Json& value) {
  static const std::array<std::string_view, 9> kKeys = {
      "font_resource_id", "font_content_hash", "fallback_chain", "font_size", "rgba",
      "weight", "italic", "locale", "attributes"};
  if (!value.is_object() || value.size() != kKeys.size() ||
      !std::ranges::all_of(kKeys, [&value](std::string_view key) {
        return value.contains(key);
      })) {
    throw std::invalid_argument("snapshot style schema mismatch");
  }
  TextStyle style;
  style.font_resource_id = value.at("font_resource_id").get<std::string>();
  style.font_content_hash = value.at("font_content_hash").get<std::string>();
  style.fallback_chain.clear();
  for (const Json& fallback : value.at("fallback_chain")) {
    if (!fallback.is_object() || fallback.size() != 2 ||
        !fallback.contains("resource_id") || !fallback.contains("content_hash")) {
      throw std::invalid_argument("snapshot fallback chain schema mismatch");
    }
    style.fallback_chain.push_back(
        {fallback.at("resource_id").get<std::string>(),
         fallback.at("content_hash").get<std::string>()});
  }
  style.font_size = value.at("font_size").get<float>();
  style.rgba = value.at("rgba").get<uint32_t>();
  style.weight = value.at("weight").get<uint16_t>();
  style.italic = value.at("italic").get<bool>();
  style.locale = value.at("locale").get<std::string>();
  style.attributes = value.at("attributes").get<decltype(style.attributes)>();
  ValidateStyle(style);
  return style;
}

std::vector<Paragraph> Inflate(const StyledText& flat) {
  if (!flat.valid()) {
    throw std::invalid_argument("styled text must provide one style per UTF-16 unit");
  }
  std::vector<Paragraph> result(1);
  auto append = [&result](char16_t unit, const TextStyle& style) {
    Paragraph& paragraph = result.back();
    if (paragraph.runs.empty() || paragraph.runs.back().style != style) {
      paragraph.runs.push_back(TextRun{{}, style});
    }
    paragraph.runs.back().text.push_back(unit);
  };
  for (size_t index = 0; index < flat.text.size(); ++index) {
    if (flat.text[index] == u'\n') {
      result.emplace_back();
    } else {
      append(flat.text[index], flat.styles[index]);
    }
  }
  return result;
}

void AppendRun(std::vector<TextRun>* runs, std::u16string text,
               const TextStyle& style) {
  if (text.empty()) return;
  if (!runs->empty() && runs->back().style == style) {
    runs->back().text += std::move(text);
    return;
  }
  runs->push_back({std::move(text), style});
}

void AppendStyledText(std::vector<TextRun>* runs, const StyledText& text) {
  if (text.text.empty()) return;
  size_t begin = 0;
  for (size_t index = 1; index <= text.text.size(); ++index) {
    if (index != text.text.size() && text.styles[index] == text.styles[begin]) {
      continue;
    }
    AppendRun(runs, text.text.substr(begin, index - begin), text.styles[begin]);
    begin = index;
  }
}

bool ReplaceCollapsedWithoutNewline(Paragraph* paragraph, uint32_t offset,
                                    const StyledText& inserted) {
  if (paragraph == nullptr || inserted.text.empty() ||
      inserted.text.find(u'\n') != std::u16string::npos) {
    return false;
  }

  std::vector<TextRun> replacement;
  replacement.reserve(paragraph->runs.size() + 1);
  uint32_t remaining = offset;
  bool inserted_text = false;
  for (size_t run_index = 0; run_index < paragraph->runs.size(); ++run_index) {
    const TextRun& run = paragraph->runs[run_index];
    if (remaining > run.text.size()) {
      AppendRun(&replacement, run.text, run.style);
      remaining -= static_cast<uint32_t>(run.text.size());
      continue;
    }
    const size_t split = remaining;
    AppendRun(&replacement, run.text.substr(0, split), run.style);
    AppendStyledText(&replacement, inserted);
    inserted_text = true;
    AppendRun(&replacement, run.text.substr(split), run.style);
    for (size_t following = run_index + 1; following < paragraph->runs.size();
         ++following) {
      AppendRun(&replacement, paragraph->runs[following].text,
                paragraph->runs[following].style);
    }
    break;
  }
  if (!inserted_text) {
    // An empty paragraph has no run to split, or the offset is at the end of
    // the final run. Both cases append at the paragraph end.
    AppendStyledText(&replacement, inserted);
  }
  paragraph->runs = std::move(replacement);
  return true;
}

uint64_t FlatOffset(const std::vector<Paragraph>& paragraphs,
                    LogicalPosition position) {
  if (position.paragraph >= paragraphs.size()) {
    throw std::out_of_range("paragraph position is outside the document");
  }
  uint64_t offset = 0;
  for (uint32_t paragraph = 0; paragraph < position.paragraph; ++paragraph) {
    for (const TextRun& run : paragraphs[paragraph].runs) {
      offset += run.text.size();
    }
    ++offset;
  }
  uint64_t paragraph_size = 0;
  for (const TextRun& run : paragraphs[position.paragraph].runs) {
    paragraph_size += run.text.size();
  }
  if (position.offset_utf16 > paragraph_size) {
    throw std::out_of_range("UTF-16 position is outside the paragraph");
  }
  return offset + position.offset_utf16;
}

}  // namespace

TextRange TextRange::Normalized() const {
  return anchor <= focus ? *this : TextRange{focus, anchor};
}

TextDocument::TextDocument() : paragraphs_(1) {}

std::u16string TextDocument::PlainText() const {
  std::u16string result;
  for (size_t paragraph_index = 0; paragraph_index < paragraphs_.size();
       ++paragraph_index) {
    const Paragraph& paragraph = paragraphs_[paragraph_index];
    for (const TextRun& run : paragraph.runs) {
      ValidateStyle(run.style);
      result += run.text;
    }
    if (paragraph_index + 1 < paragraphs_.size()) result.push_back(u'\n');
  }
  return result;
}

bool TextDocument::IsValidPosition(LogicalPosition position) const {
  try {
    static_cast<void>(FlatOffset(paragraphs_, position));
    return true;
  } catch (const std::out_of_range&) {
    return false;
  }
}

uint64_t TextDocument::FlatUtf16Offset(LogicalPosition position) const {
  return FlatOffset(paragraphs_, position);
}

LogicalPosition TextDocument::PositionAtFlatUtf16Offset(uint64_t offset) const {
  const uint64_t total = Utf16Length();
  if (offset > total) {
    throw std::out_of_range("flat UTF-16 position is outside the document");
  }
  LogicalPosition result{};
  for (const Paragraph& paragraph : paragraphs_) {
    uint64_t length = 0;
    for (const TextRun& run : paragraph.runs) length += run.text.size();
    if (offset <= length) {
      result.offset_utf16 = static_cast<uint32_t>(offset);
      return result;
    }
    offset -= length + 1;
    ++result.paragraph;
  }
  throw std::out_of_range("flat UTF-16 position is outside the document");
}

uint64_t TextDocument::Utf16Length() const {
  uint64_t length = paragraphs_.empty() ? 0 : paragraphs_.size() - 1;
  for (const Paragraph& paragraph : paragraphs_) {
    for (const TextRun& run : paragraph.runs) length += run.text.size();
  }
  return length;
}

StyledText TextDocument::Flatten() const {
  StyledText result;
  TextStyle newline_style;
  for (size_t paragraph_index = 0; paragraph_index < paragraphs_.size();
       ++paragraph_index) {
    const Paragraph& paragraph = paragraphs_[paragraph_index];
    for (const TextRun& run : paragraph.runs) {
      ValidateStyle(run.style);
      result.text += run.text;
      result.styles.insert(result.styles.end(), run.text.size(), run.style);
      newline_style = run.style;
    }
    if (paragraph_index + 1 < paragraphs_.size()) {
      result.text.push_back(u'\n');
      result.styles.push_back(newline_style);
    }
  }
  return result;
}

StyledText TextDocument::Extract(TextRange range) const {
  range = range.Normalized();
  const uint64_t start = FlatOffset(paragraphs_, range.anchor);
  const uint64_t end = FlatOffset(paragraphs_, range.focus);
  if (start == end) return {};
  const StyledText flat = Flatten();
  return {flat.text.substr(start, end - start),
          {flat.styles.begin() + static_cast<std::ptrdiff_t>(start),
           flat.styles.begin() + static_cast<std::ptrdiff_t>(end)}};
}

void TextDocument::ReplaceUnchecked(TextRange range, const StyledText& inserted) {
  if (!inserted.valid()) {
    throw std::invalid_argument("replacement styles do not match replacement text");
  }
  range = range.Normalized();
  if (range.collapsed() && !inserted.text.empty() &&
      inserted.text.find(u'\n') == std::u16string::npos) {
    if (range.anchor.paragraph >= paragraphs_.size()) {
      throw std::out_of_range("paragraph position is outside the document");
    }
    uint64_t paragraph_size = 0;
    for (const TextRun& run : paragraphs_[range.anchor.paragraph].runs) {
      paragraph_size += run.text.size();
    }
    if (range.anchor.offset_utf16 > paragraph_size) {
      throw std::out_of_range("UTF-16 position is outside the paragraph");
    }
    if (ReplaceCollapsedWithoutNewline(&paragraphs_[range.anchor.paragraph],
                                       range.anchor.offset_utf16, inserted)) {
      ++revision_;
      return;
    }
  }
  const uint64_t start = FlatOffset(paragraphs_, range.anchor);
  const uint64_t end = FlatOffset(paragraphs_, range.focus);
  StyledText flat = Flatten();
  flat.text.replace(start, end - start, inserted.text);
  flat.styles.erase(flat.styles.begin() + static_cast<std::ptrdiff_t>(start),
                    flat.styles.begin() + static_cast<std::ptrdiff_t>(end));
  flat.styles.insert(flat.styles.begin() + static_cast<std::ptrdiff_t>(start),
                     inserted.styles.begin(), inserted.styles.end());
  paragraphs_ = Inflate(flat);
  ++revision_;
}

std::string TextDocument::Digest() const {
  CanonicalEncoder encoder;
  encoder.String("canvas-poc04-text-document-v1");
  encoder.U64(last_sequence_);
  encoder.U64(paragraphs_.size());
  for (const Paragraph& paragraph : paragraphs_) {
    encoder.U64(paragraph.attributes.size());
    for (const auto& [key, value] : paragraph.attributes) {
      encoder.String(key);
      encoder.String(value);
    }
    encoder.U64(paragraph.runs.size());
    for (const TextRun& run : paragraph.runs) {
      encoder.String16(run.text);
      EncodeStyle(encoder, run.style);
    }
  }
  return Xxh3Hex(encoder.data());
}

std::string TextDocument::SnapshotJson() const {
  Json paragraphs = Json::array();
  for (const Paragraph& paragraph : paragraphs_) {
    Json runs = Json::array();
    for (const TextRun& run : paragraph.runs) {
      runs.push_back({{"text", Utf16ToUtf8(run.text)},
                      {"style", StyleToJson(run.style)}});
    }
    paragraphs.push_back({{"attributes", paragraph.attributes}, {"runs", runs}});
  }
  return Json{{"schema", "canvas-poc04-text-snapshot-v1"},
              {"last_sequence", last_sequence_},
              {"paragraphs", paragraphs}}
      .dump();
}

TextDocument TextDocument::FromSnapshotJson(std::string_view json) {
  const Json root = Json::parse(json);
  if (!root.is_object() || root.size() != 3 ||
      root.value("schema", "") != "canvas-poc04-text-snapshot-v1" ||
      !root.contains("last_sequence") || !root.contains("paragraphs") ||
      !root.at("paragraphs").is_array() || root.at("paragraphs").empty()) {
    throw std::invalid_argument("snapshot schema mismatch");
  }
  TextDocument result;
  result.last_sequence_ = root.at("last_sequence").get<uint64_t>();
  result.paragraphs_.clear();
  for (const Json& paragraph_json : root.at("paragraphs")) {
    if (!paragraph_json.is_object() || paragraph_json.size() != 2 ||
        !paragraph_json.contains("attributes") ||
        !paragraph_json.contains("runs") || !paragraph_json.at("runs").is_array()) {
      throw std::invalid_argument("snapshot paragraph schema mismatch");
    }
    Paragraph paragraph;
    paragraph.attributes = paragraph_json.at("attributes").get<decltype(paragraph.attributes)>();
    for (const Json& run_json : paragraph_json.at("runs")) {
      if (!run_json.is_object() || run_json.size() != 2 ||
          !run_json.contains("text") || !run_json.contains("style")) {
        throw std::invalid_argument("snapshot run schema mismatch");
      }
      paragraph.runs.push_back(
          {Utf8ToUtf16(run_json.at("text").get<std::string>()),
           StyleFromJson(run_json.at("style"))});
    }
    result.paragraphs_.push_back(std::move(paragraph));
  }
  return result;
}

std::u16string Utf8ToUtf16(std::string_view input) {
  std::u16string result;
  for (size_t index = 0; index < input.size();) {
    const uint8_t first = static_cast<uint8_t>(input[index]);
    uint32_t codepoint = 0;
    size_t length = 0;
    if (first <= 0x7fU) {
      codepoint = first;
      length = 1;
    } else if ((first & 0xe0U) == 0xc0U) {
      codepoint = first & 0x1fU;
      length = 2;
    } else if ((first & 0xf0U) == 0xe0U) {
      codepoint = first & 0x0fU;
      length = 3;
    } else if ((first & 0xf8U) == 0xf0U) {
      codepoint = first & 0x07U;
      length = 4;
    } else {
      throw std::invalid_argument("input is not valid UTF-8");
    }
    if (index + length > input.size()) {
      throw std::invalid_argument("input is truncated UTF-8");
    }
    for (size_t continuation = 1; continuation < length; ++continuation) {
      const uint8_t byte = static_cast<uint8_t>(input[index + continuation]);
      if ((byte & 0xc0U) != 0x80U) {
        throw std::invalid_argument("input is not valid UTF-8");
      }
      codepoint = (codepoint << 6U) | (byte & 0x3fU);
    }
    const uint32_t minimum[] = {0, 0, 0x80U, 0x800U, 0x10000U};
    if (codepoint < minimum[length] || codepoint > 0x10ffffU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
      throw std::invalid_argument("input contains invalid UTF-8 codepoint");
    }
    if (codepoint <= 0xffffU) {
      result.push_back(static_cast<char16_t>(codepoint));
    } else {
      codepoint -= 0x10000U;
      result.push_back(static_cast<char16_t>(0xd800U + (codepoint >> 10U)));
      result.push_back(static_cast<char16_t>(0xdc00U + (codepoint & 0x3ffU)));
    }
    index += length;
  }
  return result;
}

std::string Utf16ToUtf8(std::u16string_view input) {
  std::string result;
  for (size_t index = 0; index < input.size(); ++index) {
    uint32_t codepoint = input[index];
    if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
      if (index + 1 >= input.size() || input[index + 1] < 0xdc00U ||
          input[index + 1] > 0xdfffU) {
        throw std::invalid_argument("input contains unpaired UTF-16 surrogate");
      }
      codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) +
                  (input[++index] - 0xdc00U);
    } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
      throw std::invalid_argument("input contains unpaired UTF-16 surrogate");
    }
    if (codepoint <= 0x7fU) {
      result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
      result.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
      result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
      result.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
      result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
      result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
      result.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
      result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
      result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
      result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
  }
  return result;
}

}  // namespace canvas::poc04
