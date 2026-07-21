#pragma once

#include "canvas/document/document.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>

namespace canvas::embed {

class EmbeddedSurface {
 public:
  virtual ~EmbeddedSurface() = default;

  virtual void setBounds(core::Rect bounds) = 0;
  virtual void setInteractive(bool interactive) = 0;
  virtual void setVisible(bool visible) = 0;
};

class EmbeddedSurfaceFactory {
 public:
  virtual ~EmbeddedSurfaceFactory() = default;

  virtual std::unique_ptr<EmbeddedSurface> create(
      const document::Node& node) = 0;
};

class EmbeddedSurfaceManager {
 public:
  explicit EmbeddedSurfaceManager(EmbeddedSurfaceFactory& factory)
      : factory_(factory) {}

  void sync(const document::Document& document, core::Rect viewport,
            const std::optional<document::NodeId>& activeNode);

  std::size_t liveCount() const { return live_.size(); }

 private:
  EmbeddedSurfaceFactory& factory_;
  std::unordered_map<document::NodeId, std::unique_ptr<EmbeddedSurface>> live_;
};

}  // namespace canvas::embed
