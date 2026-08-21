#ifndef CANVAS_POC04_RICH_TEXT_H_
#define CANVAS_POC04_RICH_TEXT_H_

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::poc04 {

inline constexpr std::string_view kRobotoRegularResourceId =
    "canvas.roboto.regular";
inline constexpr std::string_view kRobotoRegularSha256 =
    "466989fd178ca6ed13641893b7003e5d6ec36e42c2a816dee71f87b775ea097f";
inline constexpr std::string_view kNotoSansCjkSubsetResourceId =
    "canvas.noto-sans-cjk.subset";
inline constexpr std::string_view kNotoSansCjkSubsetSha256 =
    "e7f71fc8aec139bb21cc541067eabb162b87aeeac0ccfcb3c835a20d0cee340a";

struct LogicalPosition {
  uint32_t paragraph = 0;
  uint32_t offset_utf16 = 0;
  auto operator<=>(const LogicalPosition&) const = default;
};

struct TextRange {
  LogicalPosition anchor;
  LogicalPosition focus;
  [[nodiscard]] TextRange Normalized() const;
  [[nodiscard]] bool collapsed() const { return anchor == focus; }
  bool operator==(const TextRange&) const = default;
};

struct FontResourceReference {
  std::string resource_id;
  std::string content_hash;
  bool operator==(const FontResourceReference&) const = default;
};

struct TextStyle {
  std::string font_resource_id = std::string(kRobotoRegularResourceId);
  std::string font_content_hash = std::string(kRobotoRegularSha256);
  std::vector<FontResourceReference> fallback_chain = {{
      std::string(kNotoSansCjkSubsetResourceId),
      std::string(kNotoSansCjkSubsetSha256)}};
  float font_size = 16.0F;
  uint32_t rgba = 0x111827ffU;
  uint16_t weight = 400;
  bool italic = false;
  std::string locale = "und";
  std::map<std::string, std::string, std::less<>> attributes;
  bool operator==(const TextStyle&) const = default;
};

struct TextRun {
  std::u16string text;
  TextStyle style;
  bool operator==(const TextRun&) const = default;
};

struct Paragraph {
  std::vector<TextRun> runs;
  std::map<std::string, std::string, std::less<>> attributes;
  bool operator==(const Paragraph&) const = default;
};

struct StyledText {
  std::u16string text;
  std::vector<TextStyle> styles;
  [[nodiscard]] bool valid() const { return text.size() == styles.size(); }
  bool operator==(const StyledText&) const = default;
};

class TextDocument {
 public:
  TextDocument();

  [[nodiscard]] const std::vector<Paragraph>& paragraphs() const {
    return paragraphs_;
  }
  [[nodiscard]] uint64_t revision() const { return revision_; }
  [[nodiscard]] uint64_t last_sequence() const { return last_sequence_; }
  [[nodiscard]] std::u16string PlainText() const;
  [[nodiscard]] bool IsValidPosition(LogicalPosition position) const;
  [[nodiscard]] uint64_t FlatUtf16Offset(LogicalPosition position) const;
  [[nodiscard]] LogicalPosition PositionAtFlatUtf16Offset(uint64_t offset) const;
  [[nodiscard]] uint64_t Utf16Length() const;
  [[nodiscard]] StyledText Extract(TextRange range) const;
  [[nodiscard]] std::string Digest() const;
  [[nodiscard]] std::string SnapshotJson() const;
  static TextDocument FromSnapshotJson(std::string_view json);

 private:
  friend class TextOperationEngine;
  [[nodiscard]] StyledText Flatten() const;
  void ReplaceUnchecked(TextRange range, const StyledText& inserted);
  void SetLastSequence(uint64_t value) { last_sequence_ = value; }

  std::vector<Paragraph> paragraphs_;
  uint64_t revision_ = 0;
  uint64_t last_sequence_ = 0;
};

struct ReplaceTextOperation {
  TextRange range;
  StyledText inserted;
  bool operator==(const ReplaceTextOperation&) const = default;
};

struct TextTransaction {
  uint64_t sequence = 0;
  std::string origin;
  std::vector<ReplaceTextOperation> changes;
  bool operator==(const TextTransaction&) const = default;
};

struct AppliedTransaction {
  TextTransaction forward;
  TextTransaction inverse;
};

class TextOperationEngine {
 public:
  static AppliedTransaction Apply(TextDocument& document,
                                  TextTransaction transaction);
  static void ReplayNdjson(TextDocument& document, std::string_view ndjson);
  static std::string ToNdjson(const std::vector<TextTransaction>& operations);
};

struct Selection {
  LogicalPosition anchor;
  LogicalPosition focus;
  [[nodiscard]] TextRange range() const { return {anchor, focus}; }
  bool operator==(const Selection&) const = default;
};

struct CompositionState {
  TextRange replacement_range;
  std::u16string preview_text;
  uint32_t selection_start_utf16 = 0;
  uint32_t selection_end_utf16 = 0;
};

class TextEditSession {
 public:
  explicit TextEditSession(std::shared_ptr<TextDocument> document);
  ~TextEditSession();

  [[nodiscard]] const std::shared_ptr<TextDocument>& document() const {
    return document_;
  }
  [[nodiscard]] bool focused() const { return focused_; }
  [[nodiscard]] const Selection& selection() const { return selection_; }
  [[nodiscard]] const std::optional<CompositionState>& composition() const {
    return composition_;
  }
  [[nodiscard]] const std::vector<TextTransaction>& operation_log() const {
    return operation_log_;
  }

  void Focus();
  void Blur();
  void SetSelection(Selection selection);
  void BeginComposition();
  void UpdateComposition(std::u16string text, uint32_t selection_start_utf16,
                         uint32_t selection_end_utf16);
  void CommitComposition();
  void CancelComposition();
  void InsertText(std::u16string text, const TextStyle& style = {});
  void DeleteSelection();
  void DeleteSurroundingText(uint32_t before_utf16, uint32_t after_utf16);
  [[nodiscard]] std::u16string CopySelection() const;
  [[nodiscard]] std::u16string SelectedText() const;
  [[nodiscard]] std::u16string TextBeforeCursor(uint32_t max_utf16_units) const;
  [[nodiscard]] std::u16string TextAfterCursor(uint32_t max_utf16_units) const;
  void CutSelection();
  void Paste(std::u16string text, const TextStyle& style = {});
  bool Undo();
  bool Redo();
  [[nodiscard]] std::u16string SurroundingText(uint32_t max_utf16_units) const;
  [[nodiscard]] std::string OperationLogNdjson() const;

 private:
  struct UndoEntry {
    TextTransaction forward;
    TextTransaction inverse;
  };
  void CommitReplace(TextRange range, StyledText inserted, std::string origin,
                     bool record_undo);
  [[nodiscard]] LogicalPosition Advance(LogicalPosition start,
                                        std::u16string_view text) const;

  std::shared_ptr<TextDocument> document_;
  bool focused_ = false;
  Selection selection_{};
  std::optional<CompositionState> composition_;
  std::vector<TextTransaction> operation_log_;
  std::vector<UndoEntry> undo_stack_;
  std::vector<UndoEntry> redo_stack_;
};

enum class FontDiagnostic : uint8_t {
  kOk,
  kMissing,
  kHashMismatch,
  kFallbackUsed,
};

struct FontResolution {
  FontDiagnostic diagnostic = FontDiagnostic::kMissing;
  std::string requested_id;
  std::string resolved_id;
  std::string content_hash;
  uint64_t generation = 0;
  std::span<const uint8_t> bytes;
};

class FontResourceResolver {
 public:
  bool Register(std::string resource_id, std::string expected_sha256,
                std::span<const uint8_t> bytes);
  bool Declare(std::string resource_id, std::string expected_sha256);
  bool Remove(std::string_view resource_id);
  [[nodiscard]] FontResolution Resolve(
      std::string_view requested_id,
      std::span<const std::string> canonical_fallback_chain) const;
  [[nodiscard]] uint64_t generation() const { return generation_; }
  [[nodiscard]] std::string LastDiagnostic() const { return last_diagnostic_; }

 private:
  struct Resource {
    std::string hash;
    std::vector<uint8_t> bytes;
  };
  std::map<std::string, Resource, std::less<>> resources_;
  std::map<std::string, std::string, std::less<>> declarations_;
  uint64_t generation_ = 0;
  std::string last_diagnostic_;
};

struct LayoutRect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
  bool operator==(const LayoutRect&) const = default;
};

struct LineGeometry {
  LogicalPosition start;
  LogicalPosition end;
  float baseline = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

struct ClusterGeometry {
  TextRange range;
  LayoutRect bounds;
};

struct TextLayoutResult {
  float width = 0.0F;
  float height = 0.0F;
  std::vector<LineGeometry> lines;
  std::vector<ClusterGeometry> clusters;
  std::vector<LayoutRect> selection_rects;
  std::vector<std::string> diagnostics;
};

class TextLayout {
 public:
  virtual ~TextLayout() = default;
  virtual TextLayoutResult Layout(const TextDocument& document, float width,
                                  TextRange selection) = 0;
};

class DeterministicTextLayout final : public TextLayout {
 public:
  TextLayoutResult Layout(const TextDocument& document, float width,
                          TextRange selection) override;
};

std::u16string Utf8ToUtf16(std::string_view input);
std::string Utf16ToUtf8(std::u16string_view input);
std::string Sha256Hex(std::span<const uint8_t> input);

}  // namespace canvas::poc04

#endif  // CANVAS_POC04_RICH_TEXT_H_
