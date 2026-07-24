#include "canvas/storage/document_codec.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace canvas::storage {

namespace {

using Json = nlohmann::json;
using canvas::core::Rect;
using canvas::core::Vec2;
using canvas::document::Document;
using canvas::document::EmbeddedKind;
using canvas::document::LayerClass;
using canvas::document::Node;
using canvas::document::NodeId;
using canvas::document::StrokeCoordinateSpace;

constexpr int kCurrentSchemaVersion = Document::schemaVersion;

// These limits are deliberately independent of the 512 MiB file-store cap.
// They keep malformed-but-small documents from consuming unbounded native
// heap/stack while still leaving ample room for normal whiteboards.
constexpr std::size_t kMaximumNodes = 250'000U;
constexpr std::size_t kMaximumPointsPerStroke = 1'000'000U;
constexpr std::size_t kMaximumTotalPoints = 10'000'000U;
constexpr std::size_t kMaximumNodeIdBytes = 1'024U;
constexpr std::size_t kMaximumStringBytes = 1U << 20U;
constexpr std::size_t kMaximumUnknownNodeBytes = 8U << 20U;
constexpr std::size_t kMaximumParentDepth = 4'096U;
constexpr std::size_t kMaximumContainerNesting = 256U;
constexpr std::size_t kMaximumEncodedBytes = 512U * 1024U * 1024U;

[[noreturn]] void invalid(std::string_view message) {
  throw std::runtime_error(std::string(message));
}

bool isValidUtf8(std::string_view value) noexcept {
  const auto continuation = [](unsigned char byte) {
    return byte >= 0x80U && byte <= 0xBFU;
  };
  std::size_t index = 0;
  while (index < value.size()) {
    const auto lead = static_cast<unsigned char>(value[index]);
    if (lead <= 0x7FU) {
      ++index;
      continue;
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
      if (index + 1U >= value.size() ||
          !continuation(static_cast<unsigned char>(value[index + 1U]))) {
        return false;
      }
      index += 2U;
      continue;
    }
    if (lead >= 0xE0U && lead <= 0xEFU) {
      if (index + 2U >= value.size()) return false;
      const auto second = static_cast<unsigned char>(value[index + 1U]);
      const auto third = static_cast<unsigned char>(value[index + 2U]);
      const bool validSecond =
          lead == 0xE0U ? second >= 0xA0U && second <= 0xBFU
                        : lead == 0xEDU
                              ? second >= 0x80U && second <= 0x9FU
                              : continuation(second);
      if (!validSecond || !continuation(third)) return false;
      index += 3U;
      continue;
    }
    if (lead >= 0xF0U && lead <= 0xF4U) {
      if (index + 3U >= value.size()) return false;
      const auto second = static_cast<unsigned char>(value[index + 1U]);
      const auto third = static_cast<unsigned char>(value[index + 2U]);
      const auto fourth = static_cast<unsigned char>(value[index + 3U]);
      const bool validSecond =
          lead == 0xF0U ? second >= 0x90U && second <= 0xBFU
                        : lead == 0xF4U
                              ? second >= 0x80U && second <= 0x8FU
                              : continuation(second);
      if (!validSecond || !continuation(third) || !continuation(fourth)) {
        return false;
      }
      index += 4U;
      continue;
    }
    return false;
  }
  return true;
}

void requireValidUtf8(std::string_view value, std::string_view context) {
  if (!isValidUtf8(value)) {
    throw std::runtime_error(std::string(context) + " must be valid UTF-8");
  }
}

void validateJsonLexemes(std::string_view text) {
  bool inString = false;
  bool escaped = false;
  std::size_t nesting = 0;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const char value = text[index];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        inString = false;
      }
      continue;
    }
    if (value == '"') {
      inString = true;
      continue;
    }
    if (value == '{' || value == '[') {
      ++nesting;
      if (nesting > kMaximumContainerNesting) {
        invalid("JSON nesting exceeds the document limit");
      }
      continue;
    }
    if (value == '}' || value == ']') {
      if (nesting > 0) --nesting;
      continue;
    }
    if (value != '-' && (value < '0' || value > '9')) continue;

    const std::size_t start = index;
    std::size_t cursor = index;
    const bool negative = text[cursor] == '-';
    if (negative) {
      ++cursor;
      if (cursor >= text.size() || text[cursor] < '0' || text[cursor] > '9') {
        continue;  // The JSON parser reports the malformed token.
      }
    }
    const std::size_t digitsStart = cursor;
    while (cursor < text.size() && text[cursor] >= '0' &&
           text[cursor] <= '9') {
      ++cursor;
    }
    bool integerLexeme = true;
    if (cursor < text.size() && text[cursor] == '.') {
      integerLexeme = false;
      ++cursor;
      while (cursor < text.size() && text[cursor] >= '0' &&
             text[cursor] <= '9') {
        ++cursor;
      }
    }
    if (cursor < text.size() &&
        (text[cursor] == 'e' || text[cursor] == 'E')) {
      integerLexeme = false;
      ++cursor;
      if (cursor < text.size() &&
          (text[cursor] == '+' || text[cursor] == '-')) {
        ++cursor;
      }
      while (cursor < text.size() && text[cursor] >= '0' &&
             text[cursor] <= '9') {
        ++cursor;
      }
    }
    if (integerLexeme) {
      std::uint64_t magnitude = 0;
      const char* first = text.data() + digitsStart;
      const char* last = text.data() + cursor;
      const auto parsed = std::from_chars(first, last, magnitude);
      constexpr std::uint64_t kMinimumSignedMagnitude =
          std::uint64_t{1} << 63U;
      if (parsed.ec == std::errc::result_out_of_range ||
          parsed.ptr != last ||
          (negative && magnitude > kMinimumSignedMagnitude)) {
        invalid("JSON integer cannot be represented losslessly");
      }
    }
    index = cursor == start ? cursor : cursor - 1U;
  }
}

std::uint64_t readMessagePackUnsigned(
    const std::vector<std::uint8_t>& bytes, std::size_t& cursor,
    std::size_t width) {
  if (width > bytes.size() - cursor) {
    invalid("truncated MessagePack document");
  }
  std::uint64_t result = 0;
  for (std::size_t index = 0; index < width; ++index) {
    result = (result << 8U) | bytes[cursor + index];
  }
  cursor += width;
  return result;
}

void skipMessagePackBytes(const std::vector<std::uint8_t>& bytes,
                          std::size_t& cursor, std::uint64_t count) {
  if (count > static_cast<std::uint64_t>(bytes.size() - cursor)) {
    invalid("truncated MessagePack document");
  }
  cursor += static_cast<std::size_t>(count);
}

void validateMessagePackStructure(const std::vector<std::uint8_t>& bytes) {
  // nlohmann's binary reader recursively descends into containers. Preflight
  // the wire structure with an explicit stack so an attacker cannot exhaust
  // the native call stack before the decoded model reaches our validators.
  std::vector<std::uint64_t> pendingValues{1U};
  pendingValues.reserve(kMaximumContainerNesting + 1U);
  std::size_t cursor = 0;
  while (!pendingValues.empty()) {
    if (pendingValues.back() == 0U) {
      pendingValues.pop_back();
      continue;
    }
    --pendingValues.back();
    if (cursor >= bytes.size()) invalid("truncated MessagePack document");

    const std::uint8_t marker = bytes[cursor++];
    std::uint64_t childCount = 0;
    bool isContainer = false;
    if (marker <= 0x7FU || marker >= 0xE0U) {
      continue;  // Positive and negative fixed integers.
    }
    if (marker >= 0x80U && marker <= 0x8FU) {
      childCount = static_cast<std::uint64_t>(marker & 0x0FU) * 2U;
      isContainer = true;
    } else if (marker >= 0x90U && marker <= 0x9FU) {
      childCount = marker & 0x0FU;
      isContainer = true;
    } else if (marker >= 0xA0U && marker <= 0xBFU) {
      skipMessagePackBytes(bytes, cursor, marker & 0x1FU);
    } else {
      switch (marker) {
        case 0xC0U:  // nil
        case 0xC2U:  // false
        case 0xC3U:  // true
          break;
        case 0xC1U:
          invalid("MessagePack contains a reserved marker");
        case 0xC4U: {  // bin 8
          const auto size = readMessagePackUnsigned(bytes, cursor, 1U);
          skipMessagePackBytes(bytes, cursor, size);
          break;
        }
        case 0xC5U: {  // bin 16
          const auto size = readMessagePackUnsigned(bytes, cursor, 2U);
          skipMessagePackBytes(bytes, cursor, size);
          break;
        }
        case 0xC6U: {  // bin 32
          const auto size = readMessagePackUnsigned(bytes, cursor, 4U);
          skipMessagePackBytes(bytes, cursor, size);
          break;
        }
        case 0xC7U: {  // ext 8
          const auto size = readMessagePackUnsigned(bytes, cursor, 1U);
          skipMessagePackBytes(bytes, cursor, size + 1U);
          break;
        }
        case 0xC8U: {  // ext 16
          const auto size = readMessagePackUnsigned(bytes, cursor, 2U);
          skipMessagePackBytes(bytes, cursor, size + 1U);
          break;
        }
        case 0xC9U: {  // ext 32
          const auto size = readMessagePackUnsigned(bytes, cursor, 4U);
          skipMessagePackBytes(bytes, cursor, size + 1U);
          break;
        }
        case 0xCAU:
          skipMessagePackBytes(bytes, cursor, 4U);
          break;
        case 0xCBU:
          skipMessagePackBytes(bytes, cursor, 8U);
          break;
        case 0xCCU:
        case 0xD0U:
          skipMessagePackBytes(bytes, cursor, 1U);
          break;
        case 0xCDU:
        case 0xD1U:
          skipMessagePackBytes(bytes, cursor, 2U);
          break;
        case 0xCEU:
        case 0xD2U:
          skipMessagePackBytes(bytes, cursor, 4U);
          break;
        case 0xCFU:
        case 0xD3U:
          skipMessagePackBytes(bytes, cursor, 8U);
          break;
        case 0xD4U:
          skipMessagePackBytes(bytes, cursor, 2U);
          break;
        case 0xD5U:
          skipMessagePackBytes(bytes, cursor, 3U);
          break;
        case 0xD6U:
          skipMessagePackBytes(bytes, cursor, 5U);
          break;
        case 0xD7U:
          skipMessagePackBytes(bytes, cursor, 9U);
          break;
        case 0xD8U:
          skipMessagePackBytes(bytes, cursor, 17U);
          break;
        case 0xD9U: {  // str 8
          const auto size = readMessagePackUnsigned(bytes, cursor, 1U);
          skipMessagePackBytes(bytes, cursor, size);
          break;
        }
        case 0xDAU: {  // str 16
          const auto size = readMessagePackUnsigned(bytes, cursor, 2U);
          skipMessagePackBytes(bytes, cursor, size);
          break;
        }
        case 0xDBU: {  // str 32
          const auto size = readMessagePackUnsigned(bytes, cursor, 4U);
          skipMessagePackBytes(bytes, cursor, size);
          break;
        }
        case 0xDCU:
          childCount = readMessagePackUnsigned(bytes, cursor, 2U);
          isContainer = true;
          break;
        case 0xDDU:
          childCount = readMessagePackUnsigned(bytes, cursor, 4U);
          isContainer = true;
          break;
        case 0xDEU:
          childCount =
              readMessagePackUnsigned(bytes, cursor, 2U) * 2U;
          isContainer = true;
          break;
        case 0xDFU:
          childCount =
              readMessagePackUnsigned(bytes, cursor, 4U) * 2U;
          isContainer = true;
          break;
        default:
          invalid("MessagePack contains an unknown marker");
      }
    }

    if (!isContainer) continue;
    // pendingValues contains the synthetic root plus every open container, so
    // its current size is the depth that the new container would have.
    if (pendingValues.size() > kMaximumContainerNesting) {
      invalid("MessagePack nesting exceeds the document limit");
    }
    if (childCount != 0U) pendingValues.push_back(childCount);
  }
  if (cursor != bytes.size()) {
    invalid("MessagePack document contains trailing bytes");
  }
}

bool containsUnsupportedJsonValue(const Json& root) {
  std::vector<const Json*> pending{&root};
  while (!pending.empty()) {
    const Json* value = pending.back();
    pending.pop_back();
    if (value->is_binary() || value->is_discarded() ||
        (value->is_number_float() &&
         !std::isfinite(value->get<double>()))) {
      return true;
    }
    if (value->is_array()) {
      for (const auto& child : *value) pending.push_back(&child);
    } else if (value->is_object()) {
      for (const auto& entry : value->items()) {
        pending.push_back(&entry.value());
      }
    }
  }
  return false;
}

const Json& required(const Json& object, const char* key,
                     std::string_view context) {
  if (!object.is_object() || !object.contains(key)) {
    throw std::runtime_error(std::string(context) + " is missing " + key);
  }
  return object.at(key);
}

std::string requiredString(const Json& object, const char* key,
                           std::string_view context, bool allowEmpty = true) {
  const Json& value = required(object, key, context);
  if (!value.is_string()) {
    throw std::runtime_error(std::string(context) + "." + key +
                             " must be a string");
  }
  const auto& source = value.get_ref<const Json::string_t&>();
  if (source.size() > kMaximumStringBytes) {
    throw std::runtime_error(std::string(context) + "." + key +
                             " exceeds the string-size limit");
  }
  requireValidUtf8(source, std::string(context) + "." + key);
  std::string result = source;
  if (!allowEmpty && result.empty()) {
    throw std::runtime_error(std::string(context) + "." + key +
                             " must not be empty");
  }
  return result;
}

double finiteNumber(const Json& value, std::string_view context) {
  if (!value.is_number()) {
    throw std::runtime_error(std::string(context) + " must be a number");
  }
  const double number = value.get<double>();
  if (!std::isfinite(number)) {
    throw std::runtime_error(std::string(context) + " must be finite");
  }
  return number;
}

float finiteFloat(const Json& value, std::string_view context) {
  const double number = finiteNumber(value, context);
  if (number < -static_cast<double>(std::numeric_limits<float>::max()) ||
      number > static_cast<double>(std::numeric_limits<float>::max())) {
    throw std::runtime_error(std::string(context) + " is out of range");
  }
  const float result = static_cast<float>(number);
  if (!std::isfinite(result)) {
    throw std::runtime_error(std::string(context) + " must be finite");
  }
  return result;
}

std::uint64_t unsignedInteger(const Json& value, std::string_view context) {
  if (value.is_number_unsigned()) return value.get<std::uint64_t>();
  if (value.is_number_integer()) {
    const auto signedValue = value.get<std::int64_t>();
    if (signedValue >= 0) return static_cast<std::uint64_t>(signedValue);
  }
  throw std::runtime_error(std::string(context) + " must be unsigned");
}

std::uint32_t colorInteger(const Json& value, std::string_view context) {
  const auto number = unsignedInteger(value, context);
  if (number > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error(std::string(context) + " is out of range");
  }
  return static_cast<std::uint32_t>(number);
}

LayerClass layerFromString(std::string_view value) {
  if (value == "base") return LayerClass::Base;
  if (value == "embedded") return LayerClass::Embedded;
  if (value == "annotation") return LayerClass::Annotation;
  if (value == "chrome") return LayerClass::Chrome;
  throw std::runtime_error("node.layer has an unknown value");
}

const char* layerToString(LayerClass value) {
  switch (value) {
    case LayerClass::Base:
      return "base";
    case LayerClass::Embedded:
      return "embedded";
    case LayerClass::Annotation:
      return "annotation";
    case LayerClass::Chrome:
      return "chrome";
  }
  return "base";
}

EmbeddedKind embeddedKindFromString(std::string_view value) {
  if (value == "video") return EmbeddedKind::Video;
  if (value == "web") return EmbeddedKind::Web;
  if (value == "rich-text") return EmbeddedKind::RichText;
  throw std::runtime_error("embedded payload.kind has an unknown value");
}

const char* embeddedKindToString(EmbeddedKind value) {
  switch (value) {
    case EmbeddedKind::Video:
      return "video";
    case EmbeddedKind::Web:
      return "web";
    case EmbeddedKind::RichText:
      return "rich-text";
  }
  return "web";
}

StrokeCoordinateSpace coordinateSpaceFromString(std::string_view value) {
  if (value == "world") return StrokeCoordinateSpace::World;
  if (value == "parent-normalized") {
    return StrokeCoordinateSpace::ParentNormalized;
  }
  throw std::runtime_error(
      "stroke payload.coordinateSpace has an unknown value");
}

const char* coordinateSpaceToString(StrokeCoordinateSpace value) {
  switch (value) {
    case StrokeCoordinateSpace::World:
      return "world";
    case StrokeCoordinateSpace::ParentNormalized:
      return "parent-normalized";
  }
  return "world";
}

Rect decodeBounds(const Json& value, std::string_view context) {
  if (!value.is_array() || value.size() != 4U) {
    throw std::runtime_error(std::string(context) +
                             " must contain four numbers");
  }
  Rect result{finiteFloat(value.at(0), "bounds[0]"),
              finiteFloat(value.at(1), "bounds[1]"),
              finiteFloat(value.at(2), "bounds[2]"),
              finiteFloat(value.at(3), "bounds[3]")};
  if (!(result.width > 0.0F) || !(result.height > 0.0F)) {
    throw std::runtime_error(std::string(context) +
                             " dimensions must be positive");
  }
  return result;
}

std::optional<NodeId> decodeParent(const Json& object,
                                   std::string_view context) {
  if (!object.contains("parentId") || object.at("parentId").is_null()) {
    return std::nullopt;
  }
  if (!object.at("parentId").is_string()) {
    throw std::runtime_error(std::string(context) +
                             ".parentId must be a string or null");
  }
  const auto& parentValue =
      object.at("parentId").get_ref<const Json::string_t&>();
  if (parentValue.size() > kMaximumNodeIdBytes) {
    throw std::runtime_error(std::string(context) +
                             ".parentId exceeds the id-size limit");
  }
  requireValidUtf8(parentValue, std::string(context) + ".parentId");
  const std::string parent = parentValue;
  if (parent.empty()) {
    throw std::runtime_error(std::string(context) +
                             ".parentId must not be empty");
  }
  return parent;
}

document::StrokeNode decodeStroke(const Json& payload, std::string_view context,
                                  std::size_t& totalPointCount) {
  if (!payload.is_object()) {
    throw std::runtime_error(std::string(context) + " must be an object");
  }
  document::StrokeNode result;
  result.width =
      finiteFloat(required(payload, "width", context), "stroke.width");
  if (!(result.width > 0.0F)) {
    throw std::runtime_error("stroke.width must be positive");
  }
  result.colorArgb =
      colorInteger(required(payload, "colorArgb", context), "stroke.colorArgb");
  result.coordinateSpace = coordinateSpaceFromString(
      requiredString(payload, "coordinateSpace", context, false));
  const Json& points = required(payload, "points", context);
  if (!points.is_array()) {
    throw std::runtime_error("stroke.points must be an array");
  }
  if (points.size() > kMaximumPointsPerStroke ||
      points.size() > kMaximumTotalPoints - totalPointCount) {
    throw std::runtime_error("stroke points exceed the document limit");
  }
  totalPointCount += points.size();
  result.points.reserve(points.size());
  for (std::size_t index = 0; index < points.size(); ++index) {
    const Json& point = points.at(index);
    if (!point.is_array() || point.size() != 4U) {
      throw std::runtime_error(
          "stroke point must contain x, y, pressure, and timestampMicros");
    }
    const float pressure = finiteFloat(point.at(2), "stroke.pressure");
    if (pressure < 0.0F || pressure > 1.0F) {
      throw std::runtime_error("stroke.pressure must be between zero and one");
    }
    result.points.push_back(document::StrokePoint{
        Vec2{finiteFloat(point.at(0), "stroke.x"),
             finiteFloat(point.at(1), "stroke.y")},
        pressure, unsignedInteger(point.at(3), "stroke.timestampMicros")});
  }
  return result;
}

document::EmbeddedNode decodeEmbedded(const Json& payload,
                                      std::string_view type,
                                      std::string_view context) {
  if (!payload.is_object()) {
    throw std::runtime_error(std::string(context) + " must be an object");
  }
  const std::string kindString =
      requiredString(payload, "kind", context, false);
  if (kindString != type) {
    throw std::runtime_error("embedded payload.kind does not match node.type");
  }
  const EmbeddedKind kind = embeddedKindFromString(kindString);
  return document::EmbeddedNode{kind,
                                requiredString(payload, "source", context,
                                               false),
                                requiredString(payload, "title", context)};
}

Node decodeNode(const Json& value, std::optional<NodeId>& parent,
                std::size_t& totalPointCount) {
  if (!value.is_object()) invalid("node must be an object");
  const std::string id = requiredString(value, "id", "node", false);
  if (id.size() > kMaximumNodeIdBytes) {
    invalid("node.id exceeds the document limit");
  }
  const std::string type = requiredString(value, "type", "node", false);
  Node node;
  node.id = id;
  node.layer = layerFromString(requiredString(value, "layer", "node", false));
  node.bounds = decodeBounds(required(value, "bounds", "node"), "node.bounds");
  parent = decodeParent(value, "node");
  const Json& payload = required(value, "payload", "node");
  if (type == "stroke") {
    node.payload = decodeStroke(payload, "node.payload", totalPointCount);
  } else if (type == "video" || type == "web" || type == "rich-text") {
    if (node.layer != LayerClass::Embedded) {
      invalid("embedded node type must use the embedded layer");
    }
    node.payload = decodeEmbedded(payload, type, "node.payload");
  } else {
    if (containsUnsupportedJsonValue(value)) {
      invalid("unknown node contains a non-JSON value");
    }
    std::string raw = value.dump();
    if (raw.size() > kMaximumUnknownNodeBytes) {
      invalid("unknown node exceeds the retention limit");
    }
    node.payload = document::UnknownNode{type, std::move(raw)};
  }
  return node;
}

bool isValidLayer(LayerClass value) noexcept {
  switch (value) {
    case LayerClass::Base:
    case LayerClass::Embedded:
    case LayerClass::Annotation:
    case LayerClass::Chrome:
      return true;
  }
  return false;
}

bool isValidEmbeddedKind(EmbeddedKind value) noexcept {
  switch (value) {
    case EmbeddedKind::Video:
    case EmbeddedKind::Web:
    case EmbeddedKind::RichText:
      return true;
  }
  return false;
}

bool isValidCoordinateSpace(StrokeCoordinateSpace value) noexcept {
  switch (value) {
    case StrokeCoordinateSpace::World:
    case StrokeCoordinateSpace::ParentNormalized:
      return true;
  }
  return false;
}

bool isFiniteRect(const Rect& value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.width) && std::isfinite(value.height) &&
         value.width > 0.0F && value.height > 0.0F;
}

void validateParentGraph(const std::vector<Node>& nodes) {
  std::unordered_map<NodeId, std::size_t> indexes;
  indexes.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const Node& node = nodes[index];
    if (node.id.empty() || node.id.size() > kMaximumNodeIdBytes ||
        !indexes.emplace(node.id, index).second) {
      invalid("document contains an invalid or duplicate node id");
    }
    requireValidUtf8(node.id, "node.id");
  }
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (!nodes[index].parentId) continue;
    if (nodes[index].parentId->empty() ||
        nodes[index].parentId->size() > kMaximumNodeIdBytes) {
      invalid("document contains an invalid parent id");
    }
    requireValidUtf8(*nodes[index].parentId, "node.parentId");
    const auto parent = indexes.find(*nodes[index].parentId);
    if (parent == indexes.end()) {
      invalid("document contains an orphan parent reference");
    }
    if (parent->second == index) {
      invalid("document contains a self parent reference");
    }
  }

  std::vector<unsigned char> marks(nodes.size(), 0);
  std::vector<std::size_t> depths(nodes.size(), 0U);
  std::vector<std::size_t> path;
  path.reserve(64U);
  for (std::size_t start = 0; start < nodes.size(); ++start) {
    if (marks[start] == 2U) continue;
    path.clear();
    std::size_t current = start;
    while (marks[current] == 0U) {
      marks[current] = 1U;
      path.push_back(current);
      if (!nodes[current].parentId) break;
      current = indexes.at(*nodes[current].parentId);
    }
    const bool terminalInPath =
        marks[current] == 1U && !nodes[current].parentId;
    if (marks[current] == 1U && !terminalInPath) {
      invalid("document contains a parent cycle");
    }
    std::size_t depth = marks[current] == 2U ? depths[current] : 0U;
    if (path.size() > kMaximumParentDepth - depth) {
      invalid("document parent depth exceeds the limit");
    }
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
      depths[*it] = ++depth;
      marks[*it] = 2U;
    }
  }
}

void validateDocumentModel(const Document& document) {
  const auto& nodes = document.nodes();
  if (nodes.size() > kMaximumNodes) {
    invalid("document contains too many nodes");
  }
  std::size_t totalPointCount = 0;
  for (const Node& node : nodes) {
    if (!isValidLayer(node.layer) || !isFiniteRect(node.bounds)) {
      invalid("node contains an invalid layer or bounds");
    }
    if (const auto* stroke = std::get_if<document::StrokeNode>(&node.payload)) {
      if (!std::isfinite(stroke->width) || !(stroke->width > 0.0F) ||
          !isValidCoordinateSpace(stroke->coordinateSpace)) {
        invalid("stroke contains invalid width or coordinate space");
      }
      if (stroke->coordinateSpace == StrokeCoordinateSpace::ParentNormalized &&
          !node.parentId) {
        invalid("parent-normalized stroke is missing its parent");
      }
      if (stroke->points.size() > kMaximumPointsPerStroke ||
          stroke->points.size() > kMaximumTotalPoints - totalPointCount) {
        invalid("stroke points exceed the document limit");
      }
      totalPointCount += stroke->points.size();
      for (const auto& point : stroke->points) {
        if (!std::isfinite(point.position.x) ||
            !std::isfinite(point.position.y) ||
            !std::isfinite(point.pressure) || point.pressure < 0.0F ||
            point.pressure > 1.0F) {
          invalid("stroke contains an invalid point");
        }
      }
    } else if (const auto* embedded =
                   std::get_if<document::EmbeddedNode>(&node.payload)) {
      if (node.layer != LayerClass::Embedded ||
          !isValidEmbeddedKind(embedded->kind) || embedded->source.empty() ||
          embedded->source.size() > kMaximumStringBytes ||
          embedded->title.size() > kMaximumStringBytes) {
        invalid("embedded node contains invalid metadata");
      }
      requireValidUtf8(embedded->source, "embedded.source");
      requireValidUtf8(embedded->title, "embedded.title");
    } else if (const auto* unknown =
                   std::get_if<document::UnknownNode>(&node.payload)) {
      if (unknown->typeName.empty() ||
          unknown->typeName.size() > kMaximumStringBytes ||
          unknown->typeName == "stroke" || unknown->typeName == "video" ||
          unknown->typeName == "web" || unknown->typeName == "rich-text" ||
          unknown->rawJson.size() > kMaximumUnknownNodeBytes) {
        invalid("unknown node contains invalid metadata");
      }
      requireValidUtf8(unknown->typeName, "unknown.typeName");
      validateJsonLexemes(unknown->rawJson);
      const Json raw = Json::parse(unknown->rawJson);
      if (!raw.is_object() || containsUnsupportedJsonValue(raw)) {
        invalid("unknown node is not representable as canonical JSON");
      }
    } else {
      invalid("node payload has an unsupported variant");
    }
  }
  validateParentGraph(nodes);
}

Json encodeNode(const Node& node) {
  if (const auto* unknown = std::get_if<document::UnknownNode>(&node.payload)) {
    validateJsonLexemes(unknown->rawJson);
    Json raw = Json::parse(unknown->rawJson);
    if (!raw.is_object()) invalid("unknown node is not a JSON object");
    // Common fields belong to the current model and may have been moved or
    // re-parented by this client. Future payload/extra fields remain untouched.
    raw["id"] = node.id;
    raw["type"] = unknown->typeName;
    raw["layer"] = layerToString(node.layer);
    raw["bounds"] = {node.bounds.x, node.bounds.y, node.bounds.width,
                     node.bounds.height};
    raw["parentId"] = node.parentId ? Json(*node.parentId) : Json(nullptr);
    if (!raw.contains("payload")) raw["payload"] = Json::object();
    return raw;
  }

  Json result{
      {"id", node.id},
      {"layer", layerToString(node.layer)},
      {"bounds",
       {node.bounds.x, node.bounds.y, node.bounds.width, node.bounds.height}},
      {"parentId", node.parentId ? Json(*node.parentId) : Json(nullptr)}};
  if (const auto* stroke = std::get_if<document::StrokeNode>(&node.payload)) {
    result["type"] = "stroke";
    Json points = Json::array();
    for (const auto& point : stroke->points) {
      points.push_back({point.position.x, point.position.y, point.pressure,
                        point.timestampMicros});
    }
    result["payload"] = Json{
        {"width", stroke->width},
        {"colorArgb", stroke->colorArgb},
        {"coordinateSpace", coordinateSpaceToString(stroke->coordinateSpace)},
        {"points", std::move(points)}};
  } else if (const auto* embedded =
                 std::get_if<document::EmbeddedNode>(&node.payload)) {
    if (node.layer != LayerClass::Embedded) {
      invalid("embedded node payload must use the embedded layer");
    }
    const char* kind = embeddedKindToString(embedded->kind);
    result["type"] = kind;
    result["payload"] = Json{{"kind", kind},
                             {"source", embedded->source},
                             {"title", embedded->title}};
  } else {
    invalid("node payload has an unsupported variant");
  }
  return result;
}

Json buildDocumentJson(const Document& document) {
  validateDocumentModel(document);
  Json root{{"schemaVersion", kCurrentSchemaVersion},
            {"nodes", Json::array()}};
  auto& encodedNodes = root["nodes"];
  for (const auto& node : document.nodes()) {
    encodedNodes.push_back(encodeNode(node));
  }
  return root;
}

DecodeResult decodeDocumentJson(const Json& root) {
  try {
    if (!root.is_object()) invalid("document root must be an object");
    const Json& version = required(root, "schemaVersion", "document");
    if (!version.is_number_integer() && !version.is_number_unsigned()) {
      invalid("document.schemaVersion must be an integer");
    }
    const auto versionValue =
        version.is_number_unsigned()
            ? version.get<std::uint64_t>()
            : static_cast<std::uint64_t>(version.get<std::int64_t>());
    if (versionValue != static_cast<std::uint64_t>(kCurrentSchemaVersion)) {
      invalid("unsupported document schemaVersion");
    }
    const Json& nodesValue = required(root, "nodes", "document");
    if (!nodesValue.is_array()) invalid("document.nodes must be an array");
    if (nodesValue.size() > kMaximumNodes) {
      invalid("document contains too many nodes");
    }

    std::vector<Node> nodes;
    nodes.reserve(nodesValue.size());
    std::size_t totalPointCount = 0;
    for (std::size_t index = 0; index < nodesValue.size(); ++index) {
      std::optional<NodeId> parent;
      Node node = decodeNode(nodesValue.at(index), parent, totalPointCount);
      node.parentId = std::move(parent);
      nodes.push_back(std::move(node));
    }

    validateParentGraph(nodes);
    Document document;
    if (!document.replaceValidatedNodes(std::move(nodes))) {
      invalid("unable to construct the decoded document");
    }
    validateDocumentModel(document);
    return DecodeResult{std::move(document), {}};
  } catch (const nlohmann::json::exception& error) {
    return DecodeResult{std::nullopt, error.what()};
  } catch (const std::exception& error) {
    return DecodeResult{std::nullopt, error.what()};
  }
}

}  // namespace

std::vector<std::uint8_t> DocumentCodec::encode(
    const document::Document& document) {
  try {
    const Json root = buildDocumentJson(document);
    std::vector<std::uint8_t> bytes = Json::to_msgpack(root);
    if (bytes.size() > kMaximumEncodedBytes) return {};
    return bytes;
  } catch (const std::exception&) {
    return {};
  }
}

DecodeResult DocumentCodec::decode(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() > kMaximumEncodedBytes) {
    return DecodeResult{std::nullopt,
                        "document exceeds the encoded-size limit"};
  }
  try {
    validateMessagePackStructure(bytes);
    const Json root = Json::from_msgpack(bytes);
    return decodeDocumentJson(root);
  } catch (const nlohmann::json::exception& error) {
    return DecodeResult{std::nullopt, error.what()};
  } catch (const std::exception& error) {
    return DecodeResult{std::nullopt, error.what()};
  }
}

bool DocumentCodec::decodeInto(const std::vector<std::uint8_t>& bytes,
                               document::Document& target, std::string& error) {
  DecodeResult decoded = decode(bytes);
  if (!decoded.document) {
    error = std::move(decoded.error);
    return false;
  }
  target = std::move(*decoded.document);
  error.clear();
  return true;
}

std::vector<std::uint8_t> DocumentCodec::encodeJson(
    const document::Document& document) {
  try {
    const Json root = buildDocumentJson(document);
    const std::string text = root.dump();
    if (text.size() > kMaximumEncodedBytes) return {};
    return std::vector<std::uint8_t>(text.begin(), text.end());
  } catch (const std::exception&) {
    return {};
  }
}

DecodeResult DocumentCodec::decodeJson(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() > kMaximumEncodedBytes) {
    return DecodeResult{std::nullopt,
                        "document exceeds the encoded-size limit"};
  }
  try {
    const std::string text(bytes.begin(), bytes.end());
    validateJsonLexemes(text);
    return decodeDocumentJson(Json::parse(text));
  } catch (const nlohmann::json::exception& error) {
    return DecodeResult{std::nullopt, error.what()};
  } catch (const std::exception& error) {
    return DecodeResult{std::nullopt, error.what()};
  }
}

}  // namespace canvas::storage
