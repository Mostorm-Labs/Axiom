#pragma once

#include "canvas/core/geometry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace canvas::document {

using NodeId = std::string;

enum class LayerClass { Base, Embedded, Annotation, Chrome };
enum class EmbeddedKind { Video, Web, RichText };
enum class StrokeCoordinateSpace { World, ParentNormalized };

struct StrokePoint {
  core::Vec2 position;
  float pressure = 0.5F;
  std::uint64_t timestampMicros = 0;
};

struct StrokeNode {
  std::vector<StrokePoint> points;
  float width = 4.0F;
  std::uint32_t colorArgb = 0xFF111111;
  StrokeCoordinateSpace coordinateSpace = StrokeCoordinateSpace::World;
};

struct EmbeddedNode {
  EmbeddedKind kind = EmbeddedKind::Web;
  std::string source;
  std::string title;
};

struct UnknownNode {
  std::string typeName;
  std::string rawJson;
};

using NodePayload = std::variant<StrokeNode, EmbeddedNode, UnknownNode>;

struct Node {
  NodeId id;
  LayerClass layer = LayerClass::Base;
  core::Rect bounds;
  std::optional<NodeId> parentId;
  NodePayload payload = StrokeNode{};
  std::uint64_t revision = 0;
  std::uint64_t nonAppendRevision = 0;
};

}  // namespace canvas::document
