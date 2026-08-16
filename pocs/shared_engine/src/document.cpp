#include "document.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace canvas::poc01 {
namespace {

void EncodeColor(CanonicalEncoder& encoder, Color color) {
  encoder.U8(color.r);
  encoder.U8(color.g);
  encoder.U8(color.b);
  encoder.U8(color.a);
}

void EncodeHeader(CanonicalEncoder& encoder, const NodeHeader& header) {
  encoder.U64(header.id);
  encoder.U32(static_cast<uint32_t>(header.order));
  encoder.F32(header.translation_x);
  encoder.F32(header.translation_y);
}

}  // namespace

const NodeHeader& Header(const Node& node) {
  return std::visit([](const auto& value) -> const NodeHeader& {
    return value.header;
  }, node);
}

NodeHeader& Header(Node& node) {
  return std::visit([](auto& value) -> NodeHeader& { return value.header; },
                    node);
}

canvas_poc_status_t AssetRegistry::Register(std::string key,
                                             std::span<const uint8_t> bytes) {
  if (key.empty() || bytes.empty()) {
    SetLastError("asset key and bytes must be non-empty");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  if (assets_.contains(key)) {
    SetLastError("asset key is already registered: " + key);
    return CANVAS_POC_STATUS_ALREADY_EXISTS;
  }
  Asset asset;
  asset.bytes.assign(bytes.begin(), bytes.end());
  asset.content_hash = HashBytes(asset.bytes);
  assets_.emplace(std::move(key), std::move(asset));
  return CANVAS_POC_STATUS_OK;
}

const Asset* AssetRegistry::Find(std::string_view key) const {
  const auto iterator = assets_.find(key);
  return iterator == assets_.end() ? nullptr : &iterator->second;
}

Document::Document(std::shared_ptr<AssetRegistry> assets, uint32_t page_width,
                   uint32_t page_height, Color background)
    : assets_(std::move(assets)) {
  state_.page_width = page_width;
  state_.page_height = page_height;
  state_.background = background;
}

std::string Document::Digest() const {
  CanonicalEncoder encoder;
  encoder.String("canvas-poc01-document-v1");
  encoder.U32(state_.page_width);
  encoder.U32(state_.page_height);
  EncodeColor(encoder, state_.background);
  encoder.U64(state_.last_sequence);
  encoder.U64(static_cast<uint64_t>(state_.nodes.size()));

  std::vector<const Node*> ordered;
  ordered.reserve(state_.nodes.size());
  for (const auto& [id, node] : state_.nodes) {
    static_cast<void>(id);
    ordered.push_back(&node);
  }
  std::sort(ordered.begin(), ordered.end(), [](const Node* left,
                                                const Node* right) {
    const NodeHeader& lhs = Header(*left);
    const NodeHeader& rhs = Header(*right);
    return lhs.order == rhs.order ? lhs.id < rhs.id : lhs.order < rhs.order;
  });

  for (const Node* node : ordered) {
    std::visit(
        [&encoder](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, RectNode>) {
            encoder.U8(1);
            EncodeHeader(encoder, value.header);
            encoder.F32(value.x);
            encoder.F32(value.y);
            encoder.F32(value.width);
            encoder.F32(value.height);
            EncodeColor(encoder, value.color);
          } else if constexpr (std::is_same_v<T, ImageNode>) {
            encoder.U8(2);
            EncodeHeader(encoder, value.header);
            encoder.F32(value.x);
            encoder.F32(value.y);
            encoder.F32(value.width);
            encoder.F32(value.height);
            encoder.String(value.asset_key);
            encoder.U64(value.asset_hash.high);
            encoder.U64(value.asset_hash.low);
          } else if constexpr (std::is_same_v<T, VectorPathNode>) {
            encoder.U8(3);
            EncodeHeader(encoder, value.header);
            EncodeColor(encoder, value.color);
            encoder.F32(value.stroke_width);
            encoder.U64(static_cast<uint64_t>(value.commands.size()));
            for (const PathCommand& command : value.commands) {
              encoder.U8(static_cast<uint8_t>(command.verb));
              encoder.U8(command.point_count);
              for (uint8_t index = 0; index < command.point_count; ++index) {
                encoder.F32(command.points[index]);
              }
            }
          } else if constexpr (std::is_same_v<T, TextNode>) {
            encoder.U8(4);
            EncodeHeader(encoder, value.header);
            encoder.F32(value.x);
            encoder.F32(value.y);
            encoder.F32(value.font_size);
            encoder.String(value.text);
            encoder.String(value.font_asset_key);
            encoder.U64(value.font_asset_hash.high);
            encoder.U64(value.font_asset_hash.low);
            EncodeColor(encoder, value.color);
          }
        },
        *node);
  }
  return HashHex(HashBytes(encoder.data()));
}

}  // namespace canvas::poc01
