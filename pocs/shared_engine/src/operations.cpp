#include "operations.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace canvas::poc01 {
namespace {

using Json = nlohmann::json;

class SchemaError : public std::runtime_error {
 public:
  explicit SchemaError(std::string message)
      : std::runtime_error(std::move(message)) {}
};

void RequireKeys(const Json& object,
                 std::initializer_list<std::string_view> keys,
                 std::string_view context) {
  if (!object.is_object()) {
    throw SchemaError(std::string(context) + " must be an object");
  }
  std::set<std::string, std::less<>> expected;
  for (std::string_view key : keys) {
    expected.emplace(key);
  }
  if (object.size() != expected.size()) {
    throw SchemaError(std::string(context) + " has missing or unknown fields");
  }
  for (const auto& [key, value] : object.items()) {
    static_cast<void>(value);
    if (!expected.contains(key)) {
      throw SchemaError(std::string(context) + " contains unknown field: " + key);
    }
  }
  for (const std::string& key : expected) {
    if (!object.contains(key)) {
      throw SchemaError(std::string(context) + " is missing field: " + key);
    }
  }
}

uint64_t Unsigned64(const Json& value, std::string_view name,
                    bool allow_zero = false) {
  if (!value.is_number_unsigned() && !value.is_number_integer()) {
    throw SchemaError(std::string(name) + " must be an unsigned integer");
  }
  if (value.is_number_integer() && value.get<int64_t>() < 0) {
    throw SchemaError(std::string(name) + " must not be negative");
  }
  const uint64_t result = value.get<uint64_t>();
  if (!allow_zero && result == 0) {
    throw SchemaError(std::string(name) + " must be non-zero");
  }
  return result;
}

int32_t Signed32(const Json& value, std::string_view name) {
  if (!value.is_number_integer()) {
    throw SchemaError(std::string(name) + " must be an integer");
  }
  const int64_t candidate = value.get<int64_t>();
  if (candidate < std::numeric_limits<int32_t>::min() ||
      candidate > std::numeric_limits<int32_t>::max()) {
    throw SchemaError(std::string(name) + " is outside int32 range");
  }
  return static_cast<int32_t>(candidate);
}

float Float32(const Json& value, std::string_view name,
              bool require_positive = false) {
  if (!value.is_number()) {
    throw SchemaError(std::string(name) + " must be a number");
  }
  const double candidate = value.get<double>();
  if (!std::isfinite(candidate) ||
      candidate > std::numeric_limits<float>::max() ||
      candidate < -std::numeric_limits<float>::max()) {
    throw SchemaError(std::string(name) + " must be a finite float32");
  }
  const float result = static_cast<float>(candidate);
  if (!IsFinite(result) || (require_positive && result <= 0.0F)) {
    throw SchemaError(std::string(name) +
                      (require_positive ? " must be positive" :
                                          " must be finite"));
  }
  return CanonicalizeF32(result);
}

std::string String(const Json& value, std::string_view name) {
  if (!value.is_string()) {
    throw SchemaError(std::string(name) + " must be a string");
  }
  std::string result = value.get<std::string>();
  if (result.empty()) {
    throw SchemaError(std::string(name) + " must be non-empty");
  }
  return result;
}

Color ParseColor(const Json& value) {
  if (!value.is_array() || value.size() != 4) {
    throw SchemaError("color must be an RGBA array with four channels");
  }
  Color color;
  uint8_t* channels[] = {&color.r, &color.g, &color.b, &color.a};
  for (size_t index = 0; index < 4; ++index) {
    if (!value[index].is_number_integer()) {
      throw SchemaError("color channels must be integers");
    }
    const int channel = value[index].get<int>();
    if (channel < 0 || channel > 255) {
      throw SchemaError("color channels must be in [0, 255]");
    }
    *channels[index] = static_cast<uint8_t>(channel);
  }
  return color;
}

NodeHeader ParseHeader(const Json& node) {
  NodeHeader header;
  header.id = Unsigned64(node.at("id"), "node.id");
  header.order = Signed32(node.at("order"), "node.order");
  return header;
}

PathCommand ParsePathCommand(const Json& command) {
  if (!command.is_object() || !command.contains("verb") ||
      !command.at("verb").is_string()) {
    throw SchemaError("path command requires a string verb");
  }
  const std::string verb = command.at("verb").get<std::string>();
  PathCommand result;
  if (verb == "M" || verb == "L") {
    RequireKeys(command, {"verb", "x", "y"}, "path command");
    result.verb = verb == "M" ? PathVerb::kMove : PathVerb::kLine;
    result.point_count = 2;
    result.points[0] = Float32(command.at("x"), "command.x");
    result.points[1] = Float32(command.at("y"), "command.y");
  } else if (verb == "C") {
    RequireKeys(command, {"verb", "x1", "y1", "x2", "y2", "x", "y"},
                "path command");
    result.verb = PathVerb::kCubic;
    result.point_count = 6;
    result.points[0] = Float32(command.at("x1"), "command.x1");
    result.points[1] = Float32(command.at("y1"), "command.y1");
    result.points[2] = Float32(command.at("x2"), "command.x2");
    result.points[3] = Float32(command.at("y2"), "command.y2");
    result.points[4] = Float32(command.at("x"), "command.x");
    result.points[5] = Float32(command.at("y"), "command.y");
  } else if (verb == "Z") {
    RequireKeys(command, {"verb"}, "path command");
    result.verb = PathVerb::kClose;
  } else {
    throw SchemaError("unknown path verb: " + verb);
  }
  return result;
}

Node ParseNode(const Json& node, const AssetRegistry& assets) {
  if (!node.is_object() || !node.contains("type") ||
      !node.at("type").is_string()) {
    throw SchemaError("node requires a string type");
  }
  const std::string type = node.at("type").get<std::string>();
  if (type == "rect") {
    RequireKeys(node,
                {"id", "type", "order", "x", "y", "width", "height",
                 "color"},
                "rect node");
    return RectNode{ParseHeader(node),
                    Float32(node.at("x"), "rect.x"),
                    Float32(node.at("y"), "rect.y"),
                    Float32(node.at("width"), "rect.width", true),
                    Float32(node.at("height"), "rect.height", true),
                    ParseColor(node.at("color"))};
  }
  if (type == "image") {
    RequireKeys(node,
                {"id", "type", "order", "x", "y", "width", "height",
                 "asset_key"},
                "image node");
    const std::string key = String(node.at("asset_key"), "image.asset_key");
    const Asset* asset = assets.Find(key);
    if (asset == nullptr) {
      throw SchemaError("image asset is not registered: " + key);
    }
    return ImageNode{ParseHeader(node),
                     Float32(node.at("x"), "image.x"),
                     Float32(node.at("y"), "image.y"),
                     Float32(node.at("width"), "image.width", true),
                     Float32(node.at("height"), "image.height", true), key,
                     asset->content_hash};
  }
  if (type == "vector_path") {
    RequireKeys(node,
                {"id", "type", "order", "commands", "color",
                 "stroke_width"},
                "vector_path node");
    const Json& commands = node.at("commands");
    if (!commands.is_array() || commands.empty()) {
      throw SchemaError("vector_path.commands must be a non-empty array");
    }
    VectorPathNode path;
    path.header = ParseHeader(node);
    path.color = ParseColor(node.at("color"));
    path.stroke_width =
        Float32(node.at("stroke_width"), "vector_path.stroke_width", true);
    path.commands.reserve(commands.size());
    for (const Json& command : commands) {
      path.commands.push_back(ParsePathCommand(command));
    }
    if (path.commands.front().verb != PathVerb::kMove) {
      throw SchemaError("vector_path must start with M");
    }
    return path;
  }
  if (type == "text") {
    RequireKeys(node,
                {"id", "type", "order", "x", "y", "font_size", "text",
                 "font_asset_key", "color"},
                "text node");
    const std::string key =
        String(node.at("font_asset_key"), "text.font_asset_key");
    const Asset* asset = assets.Find(key);
    if (asset == nullptr) {
      throw SchemaError("font asset is not registered: " + key);
    }
    return TextNode{ParseHeader(node),
                    Float32(node.at("x"), "text.x"),
                    Float32(node.at("y"), "text.y"),
                    Float32(node.at("font_size"), "text.font_size", true),
                    String(node.at("text"), "text.text"), key,
                    asset->content_hash, ParseColor(node.at("color"))};
  }
  throw SchemaError("unknown node type: " + type);
}

canvas_poc_status_t ApplyOne(DocumentState& state, const AssetRegistry& assets,
                             const Json& operation) {
  if (!operation.is_object() || !operation.contains("op") ||
      !operation.at("op").is_string()) {
    throw SchemaError("operation requires a string op");
  }
  const std::string op = operation.at("op").get<std::string>();
  const uint64_t version = Unsigned64(operation.at("v"), "v");
  if (version != 1) {
    throw SchemaError("unsupported replay schema version");
  }
  const uint64_t sequence = Unsigned64(operation.at("seq"), "seq");
  if (state.last_sequence == std::numeric_limits<uint64_t>::max() ||
      sequence != state.last_sequence + 1) {
    SetLastError("operation sequence must be contiguous; expected " +
                 std::to_string(state.last_sequence + 1) + ", got " +
                 std::to_string(sequence));
    return CANVAS_POC_STATUS_SEQUENCE_ERROR;
  }

  if (op == "create") {
    RequireKeys(operation, {"v", "seq", "op", "node"}, "create operation");
    Node node = ParseNode(operation.at("node"), assets);
    const uint64_t id = Header(node).id;
    if (state.nodes.contains(id)) {
      SetLastError("create references duplicate node id: " +
                   std::to_string(id));
      return CANVAS_POC_STATUS_ALREADY_EXISTS;
    }
    state.nodes.emplace(id, std::move(node));
  } else if (op == "move") {
    RequireKeys(operation, {"v", "seq", "op", "id", "dx", "dy"},
                "move operation");
    const uint64_t id = Unsigned64(operation.at("id"), "move.id");
    const auto iterator = state.nodes.find(id);
    if (iterator == state.nodes.end()) {
      SetLastError("move references missing node id: " + std::to_string(id));
      return CANVAS_POC_STATUS_NOT_FOUND;
    }
    NodeHeader& header = Header(iterator->second);
    const float dx = Float32(operation.at("dx"), "move.dx");
    const float dy = Float32(operation.at("dy"), "move.dy");
    const float next_x = header.translation_x + dx;
    const float next_y = header.translation_y + dy;
    if (!IsFinite(next_x) || !IsFinite(next_y)) {
      throw SchemaError("move result must remain finite float32");
    }
    header.translation_x = CanonicalizeF32(next_x);
    header.translation_y = CanonicalizeF32(next_y);
  } else if (op == "delete") {
    RequireKeys(operation, {"v", "seq", "op", "id"}, "delete operation");
    const uint64_t id = Unsigned64(operation.at("id"), "delete.id");
    if (state.nodes.erase(id) == 0) {
      SetLastError("delete references missing node id: " +
                   std::to_string(id));
      return CANVAS_POC_STATUS_NOT_FOUND;
    }
  } else {
    throw SchemaError("unknown operation type: " + op);
  }
  state.last_sequence = sequence;
  return CANVAS_POC_STATUS_OK;
}

}  // namespace

canvas_poc_status_t ApplyOperations(Document& document,
                                    std::string_view ndjson) {
  if (ndjson.empty()) {
    SetLastError("NDJSON batch must be non-empty");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  DocumentState working = document.state();
  std::istringstream stream{std::string(ndjson)};
  std::string line;
  size_t line_number = 0;
  bool applied_any = false;
  try {
    while (std::getline(stream, line)) {
      ++line_number;
      if (line.empty()) {
        throw SchemaError("blank NDJSON record");
      }
      Json operation = Json::parse(line, nullptr, true, true);
      const canvas_poc_status_t status =
          ApplyOne(working, document.assets(), operation);
      if (status != CANVAS_POC_STATUS_OK) {
        return status;
      }
      applied_any = true;
    }
    if (!applied_any) {
      throw SchemaError("NDJSON batch contains no records");
    }
  } catch (const nlohmann::json::exception& error) {
    SetLastError("NDJSON line " + std::to_string(line_number) + ": " +
                 error.what());
    return CANVAS_POC_STATUS_PARSE_ERROR;
  } catch (const SchemaError& error) {
    SetLastError("NDJSON line " + std::to_string(line_number) + ": " +
                 error.what());
    return CANVAS_POC_STATUS_PARSE_ERROR;
  }
  ++working.revision;
  document.mutable_state() = std::move(working);
  return CANVAS_POC_STATUS_OK;
}

}  // namespace canvas::poc01
