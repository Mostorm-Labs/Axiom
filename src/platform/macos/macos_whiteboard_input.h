#pragma once

#include "canvas/core/geometry.h"
#include "canvas/document/document.h"
#include "canvas/input/input_router.h"
#include "canvas/input/pointer_sample.h"
#include "canvas/stroke/stroke_builder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace canvas::macos {

enum class MacosWhiteboardInputResultKind {
  Ignored,
  Began,
  Changed,
  Finished,
  Cancelled,
  Failed,
};

struct MacosWhiteboardInputResult {
  MacosWhiteboardInputResultKind kind =
      MacosWhiteboardInputResultKind::Ignored;
  std::optional<document::LayerClass> layer;
  std::optional<core::Rect> dirtyBounds;
  bool fullRedraw = false;
};

// Pure C++ input state. Its future AppKit owner must call every method
// serially on the main thread; this class deliberately performs no locking.
class MacosWhiteboardInput {
 public:
  explicit MacosWhiteboardInput(std::shared_ptr<document::Document> document);
  MacosWhiteboardInput(const MacosWhiteboardInput&) = delete;
  MacosWhiteboardInput& operator=(const MacosWhiteboardInput&) = delete;
  MacosWhiteboardInput(MacosWhiteboardInput&&) = delete;
  MacosWhiteboardInput& operator=(MacosWhiteboardInput&&) = delete;

  MacosWhiteboardInputResult consume(const input::PointerSample& sample);
  MacosWhiteboardInputResult setMode(input::InputMode mode);
  MacosWhiteboardInputResult replaceDocument(
      std::shared_ptr<document::Document> document);

  bool active() const noexcept;
  const std::shared_ptr<document::Document>& document() const noexcept;

 private:
  struct ActiveStroke {
    ActiveStroke(std::uint64_t pointer, document::NodeId id,
                 document::LayerClass strokeLayer,
                 std::optional<document::NodeId> parent, float width)
        : pointerId(pointer),
          nodeId(std::move(id)),
          layer(strokeLayer),
          parentId(std::move(parent)),
          builder(width) {}

    std::uint64_t pointerId = 0;
    document::NodeId nodeId;
    document::LayerClass layer = document::LayerClass::Base;
    std::optional<document::NodeId> parentId;
    stroke::StrokeBuilder builder;
    core::Rect bounds;
  };

  static bool isAcceptedSample(const input::PointerSample& sample) noexcept;
  std::optional<document::NodeId> topmostEmbeddedHit(
      core::Vec2 position) const;
  std::optional<document::NodeId> allocateStrokeId();
  bool activePreviewIsValid() const noexcept;
  MacosWhiteboardInputResult begin(const input::PointerSample& sample);
  MacosWhiteboardInputResult move(const input::PointerSample& sample);
  MacosWhiteboardInputResult finish(const input::PointerSample& sample);
  MacosWhiteboardInputResult rollback(bool fullRedraw);
  MacosWhiteboardInputResult failActive();

  static constexpr float kStrokeWidth = 4.0F;
  static constexpr std::size_t kPreviewPointCapacity = 512;

  std::shared_ptr<document::Document> document_;
  input::InputRouter router_;
  input::InputMode mode_ = input::InputMode::Draw;
  std::optional<ActiveStroke> active_;
  std::uint64_t nextStrokeSequence_ = 1;
};

}  // namespace canvas::macos
