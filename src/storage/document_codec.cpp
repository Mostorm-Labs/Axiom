#include "canvas/storage/document_codec.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
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

[[noreturn]] void invalid(std::string_view message) {
  throw std::runtime_error(std::string(message));
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
  std::string result = value.get<std::string>();
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
  const std::string parent = object.at("parentId").get<std::string>();
  if (parent.empty()) {
    throw std::runtime_error(std::string(context) +
                             ".parentId must not be empty");
  }
  return parent;
}

document::StrokeNode decodeStroke(const Json& payload,
                                  std::string_view context) {
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
                                requiredString(payload, "source", context),
                                requiredString(payload, "title", context)};
}

Node decodeNode(const Json& value, std::optional<NodeId>& parent) {
  if (!value.is_object()) invalid("node must be an object");
  const std::string id = requiredString(value, "id", "node", false);
  const std::string type = requiredString(value, "type", "node", false);
  Node node;
  node.id = id;
  node.layer = layerFromString(requiredString(value, "layer", "node", false));
  node.bounds = decodeBounds(required(value, "bounds", "node"), "node.bounds");
  parent = decodeParent(value, "node");
  const Json& payload = required(value, "payload", "node");
  if (type == "stroke") {
    node.payload = decodeStroke(payload, "node.payload");
  } else if (type == "video" || type == "web" || type == "rich-text") {
    if (node.layer != LayerClass::Embedded) {
      invalid("embedded node type must use the embedded layer");
    }
    node.payload = decodeEmbedded(payload, type, "node.payload");
  } else {
    node.payload = document::UnknownNode{type, value.dump()};
  }
  return node;
}

Json encodeNode(const Node& node) {
  if (const auto* unknown = std::get_if<document::UnknownNode>(&node.payload)) {
    Json raw = Json::parse(unknown->rawJson);
    if (!raw.is_object()) invalid("unknown node is not a JSON object");
    // Unknown nodes decoded from a file already contain every common field.
    // Fill missing fields for callers constructing UnknownNode directly while
    // preserving every field supplied by a future producer.
    if (!raw.contains("id")) raw["id"] = node.id;
    if (!raw.contains("type")) raw["type"] = unknown->typeName;
    if (!raw.contains("layer")) raw["layer"] = layerToString(node.layer);
    if (!raw.contains("bounds")) {
      raw["bounds"] = {node.bounds.x, node.bounds.y, node.bounds.width,
                       node.bounds.height};
    }
    if (!raw.contains("parentId")) {
      raw["parentId"] = node.parentId ? Json(*node.parentId) : Json(nullptr);
    }
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

    std::vector<Node> nodes;
    std::vector<std::optional<NodeId>> parents;
    std::vector<NodeId> ids;
    nodes.reserve(nodesValue.size());
    parents.reserve(nodesValue.size());
    ids.reserve(nodesValue.size());
    std::unordered_map<NodeId, std::size_t> indexes;
    for (std::size_t index = 0; index < nodesValue.size(); ++index) {
      std::optional<NodeId> parent;
      Node node = decodeNode(nodesValue.at(index), parent);
      if (indexes.find(node.id) != indexes.end()) {
        invalid("document contains duplicate node id");
      }
      indexes.emplace(node.id, index);
      ids.push_back(node.id);
      parents.push_back(std::move(parent));
      node.parentId.reset();
      nodes.push_back(std::move(node));
    }

    for (std::size_t index = 0; index < parents.size(); ++index) {
      if (!parents[index]) continue;
      const auto parentIt = indexes.find(*parents[index]);
      if (parentIt == indexes.end())
        invalid("document contains an orphan parent reference");
      if (parentIt->second == index)
        invalid("document contains a self parent reference");
    }
    // Parent references form a tree/forest in the current document model. A
    // cycle would make erase and rendering semantics ambiguous, so reject it
    // before constructing the returned Document.
    std::vector<unsigned char> marks(nodes.size(), 0);
    const auto visit = [&](auto&& self, std::size_t index) -> void {
      if (marks[index] == 1) invalid("document contains a parent cycle");
      if (marks[index] == 2) return;
      marks[index] = 1;
      if (parents[index]) self(self, indexes.at(*parents[index]));
      marks[index] = 2;
    };
    for (std::size_t index = 0; index < nodes.size(); ++index)
      visit(visit, index);

    Document document;
    for (auto& node : nodes) {
      if (!document.add(std::move(node))) invalid("unable to add decoded node");
    }
    for (std::size_t index = 0; index < parents.size(); ++index) {
      if (!parents[index]) continue;
      if (!document.mutate(ids[index], [&](Node& node) {
            node.parentId = *parents[index];
          })) {
        invalid("unable to restore decoded parent reference");
      }
    }
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
    const Json root = Json::parse(
        encodeJson(document));  // canonical JSON is the single schema source
    return Json::to_msgpack(root);
  } catch (const std::exception&) {
    return {};
  }
}

DecodeResult DocumentCodec::decode(const std::vector<std::uint8_t>& bytes) {
  try {
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
    Json root{{"schemaVersion", kCurrentSchemaVersion},
              {"nodes", Json::array()}};
    for (const auto& node : document.nodes()) {
      root["nodes"].push_back(encodeNode(node));
    }
    const std::string text = root.dump();
    return std::vector<std::uint8_t>(text.begin(), text.end());
  } catch (const std::exception&) {
    return {};
  }
}

DecodeResult DocumentCodec::decodeJson(const std::vector<std::uint8_t>& bytes) {
  try {
    const std::string text(bytes.begin(), bytes.end());
    return decodeDocumentJson(Json::parse(text));
  } catch (const nlohmann::json::exception& error) {
    return DecodeResult{std::nullopt, error.what()};
  } catch (const std::exception& error) {
    return DecodeResult{std::nullopt, error.what()};
  }
}

}  // namespace canvas::storage
