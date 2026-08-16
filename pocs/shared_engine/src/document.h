#ifndef CANVAS_POC_DOCUMENT_H_
#define CANVAS_POC_DOCUMENT_H_

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "foundation.h"

namespace canvas::poc01 {

struct Color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;

  bool operator==(const Color&) const = default;
};

struct NodeHeader {
  uint64_t id = 0;
  int32_t order = 0;
  float translation_x = 0.0F;
  float translation_y = 0.0F;
};

struct RectNode {
  NodeHeader header;
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
  Color color;
};

struct ImageNode {
  NodeHeader header;
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
  std::string asset_key;
  Hash128 asset_hash;
};

enum class PathVerb : uint8_t { kMove = 1, kLine = 2, kCubic = 3, kClose = 4 };

struct PathCommand {
  PathVerb verb = PathVerb::kMove;
  std::array<float, 6> points{};
  uint8_t point_count = 0;
};

struct VectorPathNode {
  NodeHeader header;
  std::vector<PathCommand> commands;
  Color color;
  float stroke_width = 1.0F;
};

struct TextNode {
  NodeHeader header;
  float x = 0.0F;
  float y = 0.0F;
  float font_size = 16.0F;
  std::string text;
  std::string font_asset_key;
  Hash128 font_asset_hash;
  Color color;
};

using Node = std::variant<RectNode, ImageNode, VectorPathNode, TextNode>;

const NodeHeader& Header(const Node& node);
NodeHeader& Header(Node& node);

struct Asset {
  std::vector<uint8_t> bytes;
  Hash128 content_hash;
};

class AssetRegistry {
 public:
  canvas_poc_status_t Register(std::string key,
                               std::span<const uint8_t> bytes);
  [[nodiscard]] const Asset* Find(std::string_view key) const;
  [[nodiscard]] size_t size() const { return assets_.size(); }

 private:
  std::map<std::string, Asset, std::less<>> assets_;
};

struct DocumentState {
  uint32_t page_width = 800;
  uint32_t page_height = 600;
  Color background{244, 245, 247, 255};
  uint64_t revision = 0;
  uint64_t last_sequence = 0;
  std::map<uint64_t, Node> nodes;
};

class Document {
 public:
  Document(std::shared_ptr<AssetRegistry> assets, uint32_t page_width,
           uint32_t page_height, Color background);

  [[nodiscard]] const DocumentState& state() const { return state_; }
  [[nodiscard]] DocumentState& mutable_state() { return state_; }
  [[nodiscard]] const AssetRegistry& assets() const { return *assets_; }
  [[nodiscard]] std::string Digest() const;

 private:
  std::shared_ptr<AssetRegistry> assets_;
  DocumentState state_;
};

}  // namespace canvas::poc01

#endif  // CANVAS_POC_DOCUMENT_H_
