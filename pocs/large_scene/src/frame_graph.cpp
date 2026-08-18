#include "canvas/poc03/large_scene.h"

#include <algorithm>
#include <utility>

#include "internal.h"

namespace canvas::poc03 {
namespace {

constexpr size_t PassIndex(LogicalPass pass) {
  return static_cast<size_t>(pass);
}

PassRecord MakePass(LogicalPass pass, std::vector<LogicalPass> dependencies,
                    bool reserved = false) {
  return PassRecord{pass, {}, std::move(dependencies), reserved};
}

}  // namespace

std::string FrameGraph::VisualDigest() const {
  std::vector<uint8_t> bytes;
  for (const PassRecord& pass : logical_passes) {
    detail::EncodeU8(&bytes, static_cast<uint8_t>(pass.pass));
    detail::EncodeU8(&bytes, pass.reserved ? 1U : 0U);
    detail::EncodeU64(&bytes, pass.item_ids.size());
    for (const uint64_t id : pass.item_ids) {
      detail::EncodeU64(&bytes, id);
    }
  }
  return CanonicalDigest(bytes);
}

FrameGraph BuildFrame(const RuntimeScene& scene,
                      const ViewQueryResult& query,
                      const OverlayState& overlays) {
  FrameGraph graph;
  graph.logical_passes = {
      MakePass(LogicalPass::kBackground, {}),
      MakePass(LogicalPass::kContent, {LogicalPass::kBackground}),
      MakePass(LogicalPass::kInk, {LogicalPass::kContent}),
      MakePass(LogicalPass::kExternalSurface, {LogicalPass::kInk}, true),
      MakePass(LogicalPass::kOverlay, {LogicalPass::kExternalSurface}),
      MakePass(LogicalPass::kSelection, {LogicalPass::kOverlay}),
      MakePass(LogicalPass::kHud, {LogicalPass::kSelection}),
  };
  graph.logical_passes[PassIndex(LogicalPass::kBackground)].item_ids.push_back(0U);
  for (const uint32_t slot : query.visible) {
    const auto record = scene.RecordAt(slot);
    if (!record) {
      continue;
    }
    const LogicalPass pass = record->type == NodeType::kStroke
                                 ? LogicalPass::kInk
                                 : LogicalPass::kContent;
    graph.logical_passes[PassIndex(pass)].item_ids.push_back(record->id);
  }
  auto& overlay = graph.logical_passes[PassIndex(LogicalPass::kOverlay)].item_ids;
  overlay.insert(overlay.end(), overlays.editor_ids.begin(),
                 overlays.editor_ids.end());
  overlay.insert(overlay.end(), overlays.presence_ids.begin(),
                 overlays.presence_ids.end());
  auto& ink = graph.logical_passes[PassIndex(LogicalPass::kInk)].item_ids;
  ink.insert(ink.end(), overlays.preview_ids.begin(), overlays.preview_ids.end());
  auto& selection =
      graph.logical_passes[PassIndex(LogicalPass::kSelection)].item_ids;
  selection.insert(selection.end(), overlays.selection_ids.begin(),
                   overlays.selection_ids.end());
  auto& hud = graph.logical_passes[PassIndex(LogicalPass::kHud)].item_ids;
  hud.insert(hud.end(), overlays.hud_ids.begin(), overlays.hud_ids.end());
  graph.physical_passes.assign(graph.logical_passes.begin(),
                               graph.logical_passes.end());
  return graph;
}

void OptimizeFrameGraph(FrameGraph* graph) {
  if (graph == nullptr) {
    return;
  }
  graph->physical_passes.clear();
  PassRecord merged = MakePass(LogicalPass::kBackground, {});
  for (const PassRecord& logical : graph->logical_passes) {
    if (logical.reserved || logical.item_ids.empty()) {
      continue;
    }
    merged.item_ids.insert(merged.item_ids.end(), logical.item_ids.begin(),
                           logical.item_ids.end());
  }
  if (!merged.item_ids.empty()) {
    graph->physical_passes.push_back(std::move(merged));
  }
}

std::vector<uint64_t> ComposeSceneDrawList(const FrameGraph& graph) {
  std::vector<uint64_t> result;
  for (const LogicalPass pass : {LogicalPass::kContent, LogicalPass::kInk}) {
    const auto& items = graph.logical_passes[PassIndex(pass)].item_ids;
    result.insert(result.end(), items.begin(), items.end());
  }
  return result;
}

}  // namespace canvas::poc03
