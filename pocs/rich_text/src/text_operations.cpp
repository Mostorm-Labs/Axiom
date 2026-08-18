#include "canvas_poc04/rich_text.h"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace canvas::poc04 {
namespace {

using Json = nlohmann::json;

void RequireKeys(const Json& value, std::initializer_list<std::string_view> keys,
                 std::string_view context) {
  if (!value.is_object() || value.size() != keys.size()) {
    throw std::invalid_argument(std::string(context) + " schema mismatch");
  }
  for (std::string_view key : keys) {
    if (!value.contains(key)) {
      throw std::invalid_argument(std::string(context) + " is missing " +
                                  std::string(key));
    }
  }
}

Json PositionJson(LogicalPosition position) {
  return {{"paragraph", position.paragraph},
          {"offset_utf16", position.offset_utf16}};
}

LogicalPosition ParsePosition(const Json& value) {
  RequireKeys(value, {"paragraph", "offset_utf16"}, "logical position");
  return {value.at("paragraph").get<uint32_t>(),
          value.at("offset_utf16").get<uint32_t>()};
}

Json StyleJson(const TextStyle& style) {
  Json fallback_chain = Json::array();
  for (const auto& fallback : style.fallback_chain) {
    fallback_chain.push_back({{"resource_id", fallback.resource_id},
                              {"content_hash", fallback.content_hash}});
  }
  return {{"font_resource_id", style.font_resource_id},
          {"font_content_hash", style.font_content_hash},
          {"fallback_chain", fallback_chain},
          {"font_size", style.font_size},
          {"rgba", style.rgba},
          {"weight", style.weight},
          {"italic", style.italic},
          {"locale", style.locale},
          {"attributes", style.attributes}};
}

TextStyle ParseStyle(const Json& value) {
  RequireKeys(value,
              {"font_resource_id", "font_content_hash", "fallback_chain", "font_size", "rgba",
               "weight", "italic", "locale", "attributes"},
              "text style");
  TextStyle style;
  style.font_resource_id = value.at("font_resource_id").get<std::string>();
  style.font_content_hash = value.at("font_content_hash").get<std::string>();
  style.fallback_chain.clear();
  for (const Json& fallback : value.at("fallback_chain")) {
    RequireKeys(fallback, {"resource_id", "content_hash"}, "font fallback");
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
  return style;
}

Json StyledJson(const StyledText& styled) {
  Json styles = Json::array();
  for (const TextStyle& style : styled.styles) {
    styles.push_back(StyleJson(style));
  }
  return {{"text", Utf16ToUtf8(styled.text)}, {"styles_utf16", styles}};
}

StyledText ParseStyled(const Json& value) {
  RequireKeys(value, {"text", "styles_utf16"}, "styled text");
  if (!value.at("styles_utf16").is_array()) {
    throw std::invalid_argument("styles_utf16 must be an array");
  }
  StyledText result;
  result.text = Utf8ToUtf16(value.at("text").get<std::string>());
  for (const Json& style : value.at("styles_utf16")) {
    result.styles.push_back(ParseStyle(style));
  }
  if (!result.valid()) {
    throw std::invalid_argument("styles_utf16 length must match UTF-16 text length");
  }
  return result;
}

LogicalPosition Advance(LogicalPosition start, std::u16string_view text) {
  for (char16_t unit : text) {
    if (unit == u'\n') {
      ++start.paragraph;
      start.offset_utf16 = 0;
    } else {
      ++start.offset_utf16;
    }
  }
  return start;
}

Json TransactionJson(const TextTransaction& transaction) {
  Json changes = Json::array();
  for (const ReplaceTextOperation& change : transaction.changes) {
    changes.push_back(
        {{"range", {{"anchor", PositionJson(change.range.anchor)},
                     {"focus", PositionJson(change.range.focus)}}},
         {"inserted", StyledJson(change.inserted)}});
  }
  return {{"v", 1},
          {"seq", transaction.sequence},
          {"op", "text_transaction"},
          {"origin", transaction.origin},
          {"changes", changes}};
}

TextTransaction ParseTransaction(const Json& value) {
  RequireKeys(value, {"v", "seq", "op", "origin", "changes"},
              "text transaction");
  if (value.at("v") != 1 || value.at("op") != "text_transaction" ||
      !value.at("changes").is_array() || value.at("changes").empty()) {
    throw std::invalid_argument("unsupported or empty text transaction");
  }
  TextTransaction result;
  result.sequence = value.at("seq").get<uint64_t>();
  result.origin = value.at("origin").get<std::string>();
  if (result.origin.empty()) {
    throw std::invalid_argument("transaction origin must be non-empty");
  }
  for (const Json& change_json : value.at("changes")) {
    RequireKeys(change_json, {"range", "inserted"}, "text change");
    const Json& range = change_json.at("range");
    RequireKeys(range, {"anchor", "focus"}, "text range");
    result.changes.push_back(
        {{ParsePosition(range.at("anchor")), ParsePosition(range.at("focus"))},
         ParseStyled(change_json.at("inserted"))});
  }
  return result;
}

}  // namespace

AppliedTransaction TextOperationEngine::Apply(TextDocument& document,
                                              TextTransaction transaction) {
  if (transaction.sequence != document.last_sequence() + 1) {
    throw std::invalid_argument("text transaction sequence must be contiguous");
  }
  if (transaction.origin.empty() || transaction.changes.empty()) {
    throw std::invalid_argument("text transaction must have origin and changes");
  }
  // A one-change transaction can be committed with strong exception safety:
  // ReplaceUnchecked constructs the complete replacement before swapping it
  // into the document. Avoiding a full 10K-character document copy is the
  // ordinary typing fast path. Multi-change transactions retain a staging
  // document so an invalid later change cannot partially commit.
  TextDocument working_storage;
  TextDocument* working = &document;
  if (transaction.changes.size() > 1) {
    working_storage = document;
    working = &working_storage;
  }
  std::vector<ReplaceTextOperation> inverse_changes;
  inverse_changes.reserve(transaction.changes.size());
  for (const ReplaceTextOperation& change : transaction.changes) {
    const TextRange range = change.range.Normalized();
    if (!working->IsValidPosition(range.anchor) ||
        !working->IsValidPosition(range.focus) || !change.inserted.valid()) {
      throw std::invalid_argument("text transaction contains an invalid range");
    }
    StyledText removed = working->Extract(range);
    working->ReplaceUnchecked(range, change.inserted);
    inverse_changes.push_back(
        {{range.anchor, Advance(range.anchor, change.inserted.text)},
         std::move(removed)});
  }
  working->SetLastSequence(transaction.sequence);
  if (working != &document) {
    document = std::move(working_storage);
  }
  std::reverse(inverse_changes.begin(), inverse_changes.end());
  TextTransaction inverse{0, "inverse", std::move(inverse_changes)};
  return {std::move(transaction), std::move(inverse)};
}

void TextOperationEngine::ReplayNdjson(TextDocument& document,
                                      std::string_view ndjson) {
  if (ndjson.empty()) {
    throw std::invalid_argument("operation replay must not be empty");
  }
  TextDocument working = document;
  std::istringstream stream{std::string(ndjson)};
  std::string line;
  bool any = false;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      throw std::invalid_argument("operation replay contains a blank record");
    }
    Apply(working, ParseTransaction(Json::parse(line)));
    any = true;
  }
  if (!any) {
    throw std::invalid_argument("operation replay contains no records");
  }
  document = std::move(working);
}

std::string TextOperationEngine::ToNdjson(
    const std::vector<TextTransaction>& operations) {
  std::string result;
  for (const TextTransaction& transaction : operations) {
    result += TransactionJson(transaction).dump();
    result.push_back('\n');
  }
  return result;
}

}  // namespace canvas::poc04
