#ifndef CANVAS_POC03_LARGE_SCENE_H_
#define CANVAS_POC03_LARGE_SCENE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace canvas::poc03 {

struct Bounds {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;

  bool operator==(const Bounds&) const = default;
  [[nodiscard]] bool IsFiniteAndOrdered() const;
  [[nodiscard]] bool Intersects(const Bounds& other) const;
  [[nodiscard]] bool Contains(const Bounds& other) const;
  [[nodiscard]] Bounds Expanded(float amount) const;
  static Bounds Union(const Bounds& first, const Bounds& second);
};

enum class NodeType : uint8_t {
  kShape = 1,
  kImage = 2,
  kVectorPath = 3,
  kSimpleText = 4,
  kStroke = 5,
};

struct NodeRecord {
  uint64_t id = 0;
  uint32_t order = 0;
  NodeType type = NodeType::kShape;
  Bounds bounds;
  uint32_t rgba = 0xff000000U;
  uint64_t resource_key = 0;
  uint64_t content_revision = 1;
  bool locked = false;

  bool operator==(const NodeRecord&) const = default;
};

enum class OperationKind : uint8_t { kCreate, kUpdate, kDelete, kReorder };

struct Operation {
  OperationKind kind = OperationKind::kCreate;
  uint64_t id = 0;
  std::optional<NodeRecord> value;
};

struct SemanticChange {
  OperationKind kind = OperationKind::kCreate;
  uint64_t id = 0;
  std::optional<NodeRecord> before;
  std::optional<NodeRecord> after;
};

struct InvalidationHints {
  uint64_t before_revision = 0;
  uint64_t after_revision = 0;
  std::optional<Bounds> world_dirty;
};

struct ChangeSet {
  uint64_t before_revision = 0;
  uint64_t after_revision = 0;
  std::vector<SemanticChange> semantic_changes;
  std::optional<InvalidationHints> hints;
};

class Document {
 public:
  [[nodiscard]] uint64_t revision() const { return revision_; }
  [[nodiscard]] size_t active_count() const { return id_to_slot_.size(); }
  [[nodiscard]] const NodeRecord* Find(uint64_t id) const;
  [[nodiscard]] std::vector<const NodeRecord*> OrderedRecords() const;
  [[nodiscard]] std::string Digest() const;
  [[nodiscard]] size_t EstimatedBytes() const;

  bool Apply(const Operation& operation, ChangeSet* change_set,
             std::string* error);

 private:
  struct Slot {
    NodeRecord node;
    bool active = false;
  };
  uint64_t revision_ = 0;
  std::vector<Slot> slots_;
  std::vector<uint32_t> free_slots_;
  std::unordered_map<uint64_t, uint32_t> id_to_slot_;
};

struct GeneratorConfig {
  uint32_t node_count = 100000;
  uint64_t seed = 0x43414e5641533033ULL;
  uint32_t columns = 1000;
  float cell_size = 32.0F;
};

Document GenerateDocument(const GeneratorConfig& config);

class SpatialIndex {
 public:
  explicit SpatialIndex(float cell_size = 256.0F);
  void Clear(size_t slot_capacity);
  void Insert(uint32_t slot, const Bounds& bounds);
  void Remove(uint32_t slot, const Bounds& bounds);
  void Update(uint32_t slot, const Bounds& before, const Bounds& after);
  [[nodiscard]] std::vector<uint32_t> Query(const Bounds& bounds) const;
  [[nodiscard]] size_t EstimatedBytes() const;

 private:
  [[nodiscard]] std::vector<int64_t> CellsFor(const Bounds& bounds) const;
  float cell_size_;
  std::unordered_map<int64_t, std::vector<uint32_t>> cells_;
  mutable std::vector<uint32_t> marks_;
  mutable uint32_t query_generation_ = 0;
};

class RuntimeScene {
 public:
  [[nodiscard]] uint64_t source_revision() const { return source_revision_; }
  [[nodiscard]] size_t active_count() const { return id_to_slot_.size(); }
  [[nodiscard]] std::optional<NodeRecord> RecordAt(uint32_t slot) const;
  [[nodiscard]] std::optional<uint32_t> SlotFor(uint64_t id) const;
  [[nodiscard]] const std::vector<uint32_t>& draw_order() const {
    return draw_order_;
  }
  [[nodiscard]] const SpatialIndex& spatial_index() const { return spatial_index_; }
  [[nodiscard]] std::string Digest() const;
  [[nodiscard]] Bounds ContentBounds() const;
  [[nodiscard]] size_t EstimatedBytes() const;

 private:
  friend class SceneCompiler;
  uint64_t source_revision_ = 0;
  std::vector<uint64_t> ids_;
  std::vector<uint32_t> orders_;
  std::vector<NodeType> types_;
  std::vector<Bounds> bounds_;
  std::vector<uint32_t> colors_;
  std::vector<uint64_t> resource_keys_;
  std::vector<uint64_t> content_revisions_;
  std::vector<uint8_t> locked_;
  std::vector<uint8_t> active_;
  std::vector<uint32_t> free_slots_;
  std::vector<uint32_t> draw_order_;
  std::unordered_map<uint64_t, uint32_t> id_to_slot_;
  SpatialIndex spatial_index_;
};

struct CompileDiagnostics {
  size_t records_touched = 0;
  size_t spatial_records_touched = 0;
  size_t order_records_visited = 0;
  size_t full_fallbacks = 0;
  size_t rejected_hints = 0;
  Bounds authoritative_world_dirty;
};

class SceneCompiler {
 public:
  RuntimeScene CompileFull(const Document& document,
                           CompileDiagnostics* diagnostics = nullptr) const;
  bool ApplyIncremental(const Document& document, const ChangeSet& changes,
                        RuntimeScene* scene, CompileDiagnostics* diagnostics,
                        std::string* error) const;
};

struct ViewState {
  uint64_t view_id = 0;
  uint64_t view_revision = 0;
  uint64_t target_generation = 1;
  Bounds world_viewport;
  float zoom = 1.0F;
  float dpr = 1.0F;
  uint32_t pixel_width = 0;
  uint32_t pixel_height = 0;
};

struct ViewportTransform {
  float pan_x = 0.0F;
  float pan_y = 0.0F;
  float zoom = 1.0F;
};

struct ViewportGesture {
  float previous_focus_x_px = 0.0F;
  float previous_focus_y_px = 0.0F;
  float current_focus_x_px = 0.0F;
  float current_focus_y_px = 0.0F;
  float scale = 1.0F;
};

bool ApplyViewportGesture(const ViewportGesture& gesture, float dpr,
                          float minimum_zoom, float maximum_zoom,
                          const Bounds& pan_limits,
                          ViewportTransform* transform, std::string* error);

struct ViewQueryResult {
  std::vector<uint32_t> candidates;
  std::vector<uint32_t> visible;
  Bounds screen_damage;
  uint16_t scale_bucket = 0;
};

ViewQueryResult QueryView(const RuntimeScene& scene, const ViewState& view,
                          const std::optional<Bounds>& world_damage);
std::vector<uint64_t> HitTest(const RuntimeScene& scene, const ViewState& view,
                              float world_x, float world_y,
                              float tolerance);
std::optional<uint64_t> SelectFirstUnlocked(const RuntimeScene& scene,
                                            std::span<const uint64_t> hits);
std::optional<float> SnapNearestX(const RuntimeScene& scene,
                                  std::span<const uint32_t> candidates,
                                  float world_x, float tolerance);

enum class LogicalPass : uint8_t {
  kBackground,
  kContent,
  kInk,
  kExternalSurface,
  kOverlay,
  kSelection,
  kHud,
};

struct PassRecord {
  LogicalPass pass = LogicalPass::kBackground;
  std::vector<uint64_t> item_ids;
  std::vector<LogicalPass> dependencies;
  bool reserved = false;
};

struct FrameGraph {
  std::array<PassRecord, 7> logical_passes;
  std::vector<PassRecord> physical_passes;
  [[nodiscard]] std::string VisualDigest() const;
};

struct OverlayState {
  std::vector<uint64_t> editor_ids;
  std::vector<uint64_t> presence_ids;
  std::vector<uint64_t> preview_ids;
  std::vector<uint64_t> selection_ids;
  std::vector<uint64_t> hud_ids;
};

FrameGraph BuildFrame(const RuntimeScene& scene,
                      const ViewQueryResult& query,
                      const OverlayState& overlays);
void OptimizeFrameGraph(FrameGraph* graph);
std::vector<uint64_t> ComposeSceneDrawList(const FrameGraph& graph);

struct TileKey {
  uint64_t view_id = 0;
  uint64_t content_revision = 0;
  uint64_t device_generation = 0;
  uint32_t backend_capability = 0;
  uint16_t scale_bucket = 0;
  uint16_t color_space = 0;
  int32_t tile_x = 0;
  int32_t tile_y = 0;
  bool operator==(const TileKey&) const = default;
};

struct TileKeyHash {
  size_t operator()(const TileKey& key) const noexcept;
};

struct TileCacheStats {
  uint64_t hits = 0;
  uint64_t misses = 0;
  uint64_t evictions = 0;
  uint64_t invalidations = 0;
  size_t bytes = 0;
};

class TileCache {
 public:
  explicit TileCache(size_t byte_budget);
  bool Find(const TileKey& key);
  void Put(const TileKey& key, size_t bytes);
  void InvalidateWorld(uint64_t view_id, const Bounds& world_dirty,
                       float world_tile_size);
  void Clear();
  void DeviceLost(uint64_t new_generation);
  [[nodiscard]] uint64_t device_generation() const { return device_generation_; }
  [[nodiscard]] const TileCacheStats& stats() const { return stats_; }

 private:
  struct Entry { size_t bytes = 0; std::list<TileKey>::iterator lru; };
  size_t byte_budget_;
  uint64_t device_generation_ = 1;
  TileCacheStats stats_;
  std::list<TileKey> lru_;
  std::unordered_map<TileKey, Entry, TileKeyHash> entries_;
};

enum class InvalidationReason : uint32_t {
  kDocument = 1U << 0U,
  kView = 1U << 1U,
  kPreview = 1U << 2U,
  kSurface = 1U << 3U,
};

struct FrameInvalidation {
  uint64_t view_id = 0;
  uint64_t minimum_document_revision = 0;
  uint64_t minimum_view_revision = 0;
  uint64_t minimum_preview_revision = 0;
  uint64_t target_generation = 0;
  uint32_t reason_mask = 0;
};

class DeterministicFrameScheduler {
 public:
  void Invalidate(const FrameInvalidation& invalidation);
  [[nodiscard]] size_t pending_callback_count() const;
  std::optional<FrameInvalidation> Pump(uint64_t view_id,
                                        uint64_t target_generation);
  bool Present(const FrameInvalidation& frame, uint64_t target_generation);
  void DestroyView(uint64_t view_id);
  [[nodiscard]] uint64_t last_presented_revision(uint64_t view_id) const;

 private:
  std::unordered_map<uint64_t, FrameInvalidation> pending_;
  std::unordered_map<uint64_t, uint64_t> presented_;
};

std::string CanonicalDigest(std::span<const uint8_t> bytes);

}  // namespace canvas::poc03

#endif
