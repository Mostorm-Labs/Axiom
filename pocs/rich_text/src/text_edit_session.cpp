#include "canvas_poc04/rich_text.h"

#include <algorithm>
#include <stdexcept>

namespace canvas::poc04 {
namespace {

bool IsHighSurrogate(char16_t unit) {
  return unit >= 0xd800 && unit <= 0xdbff;
}

bool IsLowSurrogate(char16_t unit) {
  return unit >= 0xdc00 && unit <= 0xdfff;
}

bool SplitsSurrogatePair(std::u16string_view text, uint64_t offset) {
  return offset > 0 && offset < text.size() &&
         IsHighSurrogate(text[offset - 1]) && IsLowSurrogate(text[offset]);
}

}  // namespace

TextEditSession::TextEditSession(std::shared_ptr<TextDocument> document)
    : document_(std::move(document)) {
  if (!document_) {
    throw std::invalid_argument("TextEditSession requires a document");
  }
}

TextEditSession::~TextEditSession() { composition_.reset(); }

void TextEditSession::Focus() { focused_ = true; }

void TextEditSession::Blur() {
  CancelComposition();
  focused_ = false;
}

void TextEditSession::SetSelection(Selection selection) {
  if (!document_->IsValidPosition(selection.anchor) ||
      !document_->IsValidPosition(selection.focus)) {
    throw std::invalid_argument("selection is outside the document");
  }
  if (composition_) {
    throw std::logic_error("selection cannot change during composition");
  }
  selection_ = selection;
}

void TextEditSession::BeginComposition() {
  if (!focused_ || composition_) {
    throw std::logic_error("composition requires focus and an idle session");
  }
  composition_ = CompositionState{selection_.range().Normalized(), {}, 0, 0};
}

void TextEditSession::UpdateComposition(std::u16string text,
                                        uint32_t selection_start_utf16,
                                        uint32_t selection_end_utf16) {
  if (!composition_) {
    throw std::logic_error("composition update requires an active composition");
  }
  if (selection_start_utf16 > selection_end_utf16 ||
      selection_end_utf16 > text.size() ||
      SplitsSurrogatePair(text, selection_start_utf16) ||
      SplitsSurrogatePair(text, selection_end_utf16)) {
    throw std::invalid_argument("composition selection is outside preview text");
  }
  composition_->preview_text = std::move(text);
  composition_->selection_start_utf16 = selection_start_utf16;
  composition_->selection_end_utf16 = selection_end_utf16;
}

void TextEditSession::CommitComposition() {
  if (!composition_) {
    throw std::logic_error("composition commit requires an active composition");
  }
  CompositionState state = std::move(*composition_);
  composition_.reset();
  if (state.preview_text.empty() && state.replacement_range.collapsed()) {
    return;
  }
  const TextStyle style = {};
  CommitReplace(state.replacement_range,
                {state.preview_text,
                 std::vector<TextStyle>(state.preview_text.size(), style)},
                "ime-commit", true);
}

void TextEditSession::CancelComposition() { composition_.reset(); }

void TextEditSession::InsertText(std::u16string text, const TextStyle& style) {
  if (!focused_ || composition_) {
    throw std::logic_error("direct input requires focus and no composition");
  }
  if (text.empty() && selection_.range().collapsed()) {
    return;
  }
  CommitReplace(selection_.range().Normalized(),
                {text, std::vector<TextStyle>(text.size(), style)},
                "direct-input", true);
}

void TextEditSession::DeleteSelection() {
  if (!focused_ || composition_) {
    throw std::logic_error("delete requires focus and no composition");
  }
  if (!selection_.range().collapsed()) {
    CommitReplace(selection_.range().Normalized(), {}, "delete", true);
  }
}

void TextEditSession::DeleteSurroundingText(uint32_t before_utf16,
                                            uint32_t after_utf16) {
  if (!focused_ || composition_) {
    throw std::logic_error("delete requires focus and no composition");
  }
  const TextRange normalized = selection_.range().Normalized();
  if (!normalized.collapsed()) {
    DeleteSelection();
    return;
  }
  const uint64_t caret = document_->FlatUtf16Offset(normalized.anchor);
  uint64_t start = caret > before_utf16 ? caret - before_utf16 : 0;
  uint64_t end = std::min<uint64_t>(
      document_->Utf16Length(), caret + after_utf16);
  const std::u16string text = document_->PlainText();
  if (SplitsSurrogatePair(text, start)) {
    --start;
  }
  if (SplitsSurrogatePair(text, end)) {
    ++end;
  }
  if (start == end) return;
  CommitReplace({document_->PositionAtFlatUtf16Offset(start),
                 document_->PositionAtFlatUtf16Offset(end)},
                {}, "delete-surrounding", true);
}

std::u16string TextEditSession::CopySelection() const {
  return document_->Extract(selection_.range().Normalized()).text;
}

std::u16string TextEditSession::SelectedText() const { return CopySelection(); }

std::u16string TextEditSession::TextBeforeCursor(
    uint32_t max_utf16_units) const {
  const uint64_t caret = document_->FlatUtf16Offset(selection_.focus);
  const uint64_t start = caret > max_utf16_units ? caret - max_utf16_units : 0;
  const std::u16string text = document_->PlainText();
  const uint64_t safe_start = SplitsSurrogatePair(text, start) ? start + 1 : start;
  return text.substr(safe_start, caret - safe_start);
}

std::u16string TextEditSession::TextAfterCursor(
    uint32_t max_utf16_units) const {
  const uint64_t caret = document_->FlatUtf16Offset(selection_.focus);
  const std::u16string text = document_->PlainText();
  uint64_t end = std::min<uint64_t>(text.size(), caret + max_utf16_units);
  if (SplitsSurrogatePair(text, end)) --end;
  return text.substr(caret, end - caret);
}

void TextEditSession::CutSelection() {
  static_cast<void>(CopySelection());
  DeleteSelection();
}

void TextEditSession::Paste(std::u16string text, const TextStyle& style) {
  InsertText(std::move(text), style);
}

void TextEditSession::CommitReplace(TextRange range, StyledText inserted,
                                    std::string origin, bool record_undo) {
  const LogicalPosition caret = Advance(range.Normalized().anchor, inserted.text);
  TextTransaction transaction{document_->last_sequence() + 1, std::move(origin),
                              {{{range.anchor, range.focus},
                                std::move(inserted)}}};
  AppliedTransaction applied = TextOperationEngine::Apply(*document_, transaction);
  operation_log_.push_back(applied.forward);
  if (record_undo) {
    undo_stack_.push_back({applied.forward, applied.inverse});
    redo_stack_.clear();
  }
  selection_ = {caret, caret};
}

bool TextEditSession::Undo() {
  if (composition_ || undo_stack_.empty()) {
    return false;
  }
  UndoEntry entry = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  TextTransaction inverse = entry.inverse;
  inverse.sequence = document_->last_sequence() + 1;
  inverse.origin = "undo";
  const LogicalPosition caret = inverse.changes.front().range.Normalized().anchor;
  AppliedTransaction applied = TextOperationEngine::Apply(*document_, inverse);
  operation_log_.push_back(applied.forward);
  selection_ = {caret, caret};
  redo_stack_.push_back(std::move(entry));
  return true;
}

bool TextEditSession::Redo() {
  if (composition_ || redo_stack_.empty()) {
    return false;
  }
  UndoEntry entry = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  TextTransaction forward = entry.forward;
  forward.sequence = document_->last_sequence() + 1;
  forward.origin = "redo";
  const ReplaceTextOperation& change = forward.changes.back();
  const LogicalPosition caret = Advance(change.range.Normalized().anchor,
                                        change.inserted.text);
  AppliedTransaction applied = TextOperationEngine::Apply(*document_, forward);
  operation_log_.push_back(applied.forward);
  selection_ = {caret, caret};
  undo_stack_.push_back(std::move(entry));
  return true;
}

std::u16string TextEditSession::SurroundingText(
    uint32_t max_utf16_units) const {
  const std::u16string plain = document_->PlainText();
  if (plain.size() <= max_utf16_units) {
    return plain;
  }
  uint64_t caret = 0;
  const LogicalPosition position = selection_.focus;
  for (uint32_t paragraph = 0; paragraph < position.paragraph; ++paragraph) {
    for (const TextRun& run : document_->paragraphs()[paragraph].runs) {
      caret += run.text.size();
    }
    ++caret;
  }
  caret += position.offset_utf16;
  const uint64_t half = max_utf16_units / 2;
  const uint64_t start = std::min<uint64_t>(
      caret > half ? caret - half : 0, plain.size() - max_utf16_units);
  uint64_t safe_start = start;
  uint64_t safe_end = std::min<uint64_t>(plain.size(), start + max_utf16_units);
  if (SplitsSurrogatePair(plain, safe_start)) ++safe_start;
  if (SplitsSurrogatePair(plain, safe_end)) --safe_end;
  return plain.substr(safe_start, safe_end - safe_start);
}

std::string TextEditSession::OperationLogNdjson() const {
  return TextOperationEngine::ToNdjson(operation_log_);
}

LogicalPosition TextEditSession::Advance(LogicalPosition start,
                                         std::u16string_view text) const {
  for (char16_t unit : text) {
    if (unit == u'\n') {
      ++start.paragraph;
      start.offset_utf16 = 0;
    } else {
      ++start.offset_utf16;
    }
  }
  return start;
}

}  // namespace canvas::poc04
