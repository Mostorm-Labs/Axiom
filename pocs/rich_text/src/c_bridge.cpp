#include "canvas_poc04/canvas_poc04.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <string>

#include "canvas_poc04/rich_text.h"
#include "foundation.h"

namespace canvas::poc04 {
namespace {

struct Slot {
  uint16_t generation = 1;
  std::unique_ptr<TextEditSession> session;
};

std::array<Slot, 1024> g_slots;
std::mutex g_slots_mutex;

canvas_poc04_handle_t Encode(size_t index, uint16_t generation) {
  return (static_cast<uint32_t>(generation) << 16U) |
         static_cast<uint32_t>(index + 1U);
}

TextEditSession* Lookup(canvas_poc04_handle_t handle) {
  const uint32_t encoded_index = handle & 0xffffU;
  const uint16_t generation = static_cast<uint16_t>(handle >> 16U);
  if (encoded_index == 0 || encoded_index > g_slots.size()) {
    throw std::invalid_argument("invalid POC-04 handle");
  }
  Slot& slot = g_slots[encoded_index - 1U];
  if (!slot.session || slot.generation != generation) {
    throw std::invalid_argument("stale POC-04 handle");
  }
  return slot.session.get();
}

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

struct PresentedState {
  std::u16string text;
  uint64_t selection_start = 0;
  uint64_t selection_end = 0;
  std::optional<canvas_poc04_utf16_range_t> marked_range;
  std::optional<canvas_poc04_utf16_range_t> marked_selection;
};

PresentedState Present(const TextEditSession& session) {
  PresentedState state;
  const TextDocument& document = *session.document();
  state.text = document.PlainText();
  const Selection& selection = session.selection();
  state.selection_start = document.FlatUtf16Offset(selection.anchor);
  state.selection_end = document.FlatUtf16Offset(selection.focus);
  if (const auto& composition = session.composition()) {
    const TextRange replacement = composition->replacement_range.Normalized();
    const uint64_t start = document.FlatUtf16Offset(replacement.anchor);
    const uint64_t end = document.FlatUtf16Offset(replacement.focus);
    state.text.replace(start, end - start, composition->preview_text);
    state.selection_start = start + composition->selection_start_utf16;
    state.selection_end = start + composition->selection_end_utf16;
    state.marked_range = canvas_poc04_utf16_range_t{
        start, static_cast<uint64_t>(composition->preview_text.size())};
    state.marked_selection = canvas_poc04_utf16_range_t{
        state.selection_start,
        state.selection_end - state.selection_start};
  }
  return state;
}

void ValidateFlatRange(std::u16string_view text, uint64_t location,
                       uint64_t length) {
  if (location > text.size() || length > text.size() - location ||
      SplitsSurrogatePair(text, location) ||
      SplitsSurrogatePair(text, location + length)) {
    throw std::invalid_argument("flat UTF-16 range is outside text boundaries");
  }
}

float UnitAdvance(char16_t unit) {
  // The Apple recorder draws its 16 pt editor text with the platform's
  // proportional system font. An ASCII space is therefore much narrower
  // than the 9.6 px Latin probe advance below. Treating it as an ordinary
  // Latin glyph makes the Runtime caret drift farther right after every
  // space, even though the visible text remains correctly spaced.
  if (unit == u' ') return 4.2F;
  // Apple Pinyin keyboards use U+2006 (SIX-PER-EM SPACE) between marked
  // syllable units.  It is a real, narrow glyph rather than an ordinary
  // editor space; keeping its advance narrow prevents the Runtime caret from
  // drifting far to the right of the native pre-edit text.
  if (unit == u'\u2006') return 2.35F;
  if (unit >= 0xd800 && unit <= 0xdfff) return 8.0F;
  if (unit >= 0x2e80 || unit == 0xfffc) return 16.0F;
  return 9.6F;
}

struct FlatGeometry {
  float x = 0.0F;
  float y = 0.0F;
};

FlatGeometry GeometryForOffset(std::u16string_view text, uint64_t offset,
                               float layout_width) {
  ValidateFlatRange(text, offset, 0);
  FlatGeometry geometry;
  for (uint64_t index = 0; index < offset; ++index) {
    const char16_t unit = text[index];
    if (unit == u'\n') {
      geometry.x = 0.0F;
      geometry.y += 20.0F;
      continue;
    }
    const float advance = UnitAdvance(unit);
    if (geometry.x > 0.0F && geometry.x + advance > layout_width) {
      geometry.x = 0.0F;
      geometry.y += 20.0F;
    }
    geometry.x += advance;
  }
  return geometry;
}

uint64_t OffsetForPoint(std::u16string_view text, float x, float y,
                        float layout_width) {
  if (!std::isfinite(x) || !std::isfinite(y) ||
      !std::isfinite(layout_width) || layout_width <= 0.0F) {
    throw std::invalid_argument("text geometry requires finite coordinates");
  }
  const float target_y = std::max(0.0F, y);
  uint64_t best = 0;
  float best_distance = std::numeric_limits<float>::infinity();
  for (uint64_t offset = 0; offset <= text.size(); ++offset) {
    if (SplitsSurrogatePair(text, offset)) continue;
    const FlatGeometry caret = GeometryForOffset(text, offset, layout_width);
    const float dx = caret.x - std::max(0.0F, x);
    const float dy = caret.y - target_y;
    const float distance = dx * dx + dy * dy * 4.0F;
    if (distance < best_distance) {
      best_distance = distance;
      best = offset;
    }
  }
  return best;
}

template <typename Callable>
canvas_poc04_status_t Guard(Callable&& callable) {
  ClearLastError();
  try {
    std::lock_guard lock(g_slots_mutex);
    callable();
    return CANVAS_POC04_STATUS_OK;
  } catch (const std::invalid_argument& error) {
    SetLastError(error.what());
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  } catch (const std::logic_error& error) {
    SetLastError(error.what());
    return CANVAS_POC04_STATUS_INVALID_STATE;
  } catch (const std::exception& error) {
    SetLastError(error.what());
    return CANVAS_POC04_STATUS_INTERNAL_ERROR;
  } catch (...) {
    SetLastError("unknown POC-04 error");
    return CANVAS_POC04_STATUS_INTERNAL_ERROR;
  }
}

}  // namespace
}  // namespace canvas::poc04

extern "C" {

canvas_poc04_status_t canvas_poc04_session_create(
    const canvas_poc04_create_info_t* info,
    canvas_poc04_handle_t* out_session) {
  using namespace canvas::poc04;
  if (info == nullptr || out_session == nullptr ||
      info->struct_size != sizeof(*info) ||
      info->abi_version != CANVAS_POC04_ABI_VERSION) {
    SetLastError("create info has incompatible size or ABI version");
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return Guard([&] {
    for (size_t index = 0; index < g_slots.size(); ++index) {
      Slot& slot = g_slots[index];
      if (!slot.session) {
        slot.session = std::make_unique<TextEditSession>(
            std::make_shared<TextDocument>());
        *out_session = Encode(index, slot.generation);
        return;
      }
    }
    throw std::runtime_error("POC-04 handle table is full");
  });
}

canvas_poc04_status_t canvas_poc04_session_destroy(
    canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] {
    const uint32_t index = (session & 0xffffU) - 1U;
    static_cast<void>(Lookup(session));
    Slot& slot = g_slots[index];
    slot.session.reset();
    ++slot.generation;
    if (slot.generation == 0) {
      slot.generation = 1;
    }
  });
}

canvas_poc04_status_t canvas_poc04_session_focus(canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] { Lookup(session)->Focus(); });
}

canvas_poc04_status_t canvas_poc04_session_blur(canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] { Lookup(session)->Blur(); });
}

canvas_poc04_status_t canvas_poc04_session_set_selection(
    canvas_poc04_handle_t session, uint32_t anchor_paragraph,
    uint32_t anchor_offset_utf16, uint32_t focus_paragraph,
    uint32_t focus_offset_utf16) {
  using namespace canvas::poc04;
  return Guard([&] {
    Lookup(session)->SetSelection({{anchor_paragraph, anchor_offset_utf16},
                                   {focus_paragraph, focus_offset_utf16}});
  });
}

canvas_poc04_status_t canvas_poc04_session_set_selection_flat_utf16(
    canvas_poc04_handle_t session, uint64_t anchor_utf16,
    uint64_t focus_utf16) {
  using namespace canvas::poc04;
  return Guard([&] {
    TextEditSession* edit = Lookup(session);
    const PresentedState state = Present(*edit);
    ValidateFlatRange(state.text, anchor_utf16, 0);
    ValidateFlatRange(state.text, focus_utf16, 0);
    if (const auto& composition = edit->composition()) {
      const uint64_t marked_start = state.marked_range->location;
      const uint64_t marked_end = marked_start + state.marked_range->length;
      if (anchor_utf16 < marked_start || anchor_utf16 > marked_end ||
          focus_utf16 < marked_start || focus_utf16 > marked_end) {
        throw std::invalid_argument("composition selection must remain in marked text");
      }
      edit->UpdateComposition(
          composition->preview_text,
          static_cast<uint32_t>(anchor_utf16 - marked_start),
          static_cast<uint32_t>(focus_utf16 - marked_start));
      return;
    }
    edit->SetSelection(
        {edit->document()->PositionAtFlatUtf16Offset(anchor_utf16),
         edit->document()->PositionAtFlatUtf16Offset(focus_utf16)});
  });
}

canvas_poc04_status_t canvas_poc04_session_selection_flat_utf16(
    canvas_poc04_handle_t session, canvas_poc04_utf16_range_t* out_range) {
  using namespace canvas::poc04;
  if (out_range == nullptr) return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  return Guard([&] {
    const PresentedState state = Present(*Lookup(session));
    const uint64_t start = std::min(state.selection_start, state.selection_end);
    *out_range = {start, std::max(state.selection_start, state.selection_end) - start};
  });
}

canvas_poc04_status_t canvas_poc04_session_begin_composition(
    canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] { Lookup(session)->BeginComposition(); });
}

canvas_poc04_status_t canvas_poc04_session_update_composition_utf8(
    canvas_poc04_handle_t session, const char* text, size_t text_size) {
  using namespace canvas::poc04;
  if (text == nullptr && text_size != 0) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return Guard([&] {
    std::u16string value = Utf8ToUtf16({text == nullptr ? "" : text, text_size});
    if (value.size() > std::numeric_limits<uint32_t>::max()) {
      throw std::invalid_argument("composition exceeds the UTF-16 offset limit");
    }
    const auto value_size = static_cast<uint32_t>(value.size());
    Lookup(session)->UpdateComposition(value, value_size, value_size);
  });
}

canvas_poc04_status_t canvas_poc04_session_update_composition_utf8_with_selection(
    canvas_poc04_handle_t session, const char* text, size_t text_size,
    uint32_t selection_start_utf16, uint32_t selection_end_utf16) {
  using namespace canvas::poc04;
  if (text == nullptr && text_size != 0) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return Guard([&] {
    std::u16string value = Utf8ToUtf16({text == nullptr ? "" : text, text_size});
    Lookup(session)->UpdateComposition(
        std::move(value), selection_start_utf16, selection_end_utf16);
  });
}

canvas_poc04_status_t canvas_poc04_session_commit_composition(
    canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] { Lookup(session)->CommitComposition(); });
}

canvas_poc04_status_t canvas_poc04_session_cancel_composition(
    canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] { Lookup(session)->CancelComposition(); });
}

canvas_poc04_status_t canvas_poc04_session_insert_utf8(
    canvas_poc04_handle_t session, const char* text, size_t text_size) {
  using namespace canvas::poc04;
  if (text == nullptr && text_size != 0) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return Guard([&] {
    Lookup(session)->InsertText(
        Utf8ToUtf16({text == nullptr ? "" : text, text_size}));
  });
}

canvas_poc04_status_t canvas_poc04_session_delete_selection(
    canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] { Lookup(session)->DeleteSelection(); });
}

canvas_poc04_status_t canvas_poc04_session_delete_surrounding_utf16(
    canvas_poc04_handle_t session, uint32_t before_utf16,
    uint32_t after_utf16) {
  using namespace canvas::poc04;
  return Guard([&] {
    Lookup(session)->DeleteSurroundingText(before_utf16, after_utf16);
  });
}

canvas_poc04_status_t canvas_poc04_session_undo(canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] {
    if (!Lookup(session)->Undo()) {
      throw std::logic_error("nothing to undo");
    }
  });
}

canvas_poc04_status_t canvas_poc04_session_redo(canvas_poc04_handle_t session) {
  using namespace canvas::poc04;
  return Guard([&] {
    if (!Lookup(session)->Redo()) {
      throw std::logic_error("nothing to redo");
    }
  });
}

canvas_poc04_status_t canvas_poc04_session_selected_text_utf8(
    canvas_poc04_handle_t session, char* buffer, size_t buffer_size,
    size_t* out_required_size) {
  using namespace canvas::poc04;
  std::string value;
  const auto status = Guard([&] {
    value = Utf16ToUtf8(Lookup(session)->SelectedText());
  });
  return status == CANVAS_POC04_STATUS_OK
             ? CopyString(value, buffer, buffer_size, out_required_size)
             : status;
}

canvas_poc04_status_t canvas_poc04_session_text_before_cursor_utf8(
    canvas_poc04_handle_t session, uint32_t max_utf16_units, char* buffer,
    size_t buffer_size, size_t* out_required_size) {
  using namespace canvas::poc04;
  std::string value;
  const auto status = Guard([&] {
    value = Utf16ToUtf8(Lookup(session)->TextBeforeCursor(max_utf16_units));
  });
  return status == CANVAS_POC04_STATUS_OK
             ? CopyString(value, buffer, buffer_size, out_required_size)
             : status;
}

canvas_poc04_status_t canvas_poc04_session_text_after_cursor_utf8(
    canvas_poc04_handle_t session, uint32_t max_utf16_units, char* buffer,
    size_t buffer_size, size_t* out_required_size) {
  using namespace canvas::poc04;
  std::string value;
  const auto status = Guard([&] {
    value = Utf16ToUtf8(Lookup(session)->TextAfterCursor(max_utf16_units));
  });
  return status == CANVAS_POC04_STATUS_OK
             ? CopyString(value, buffer, buffer_size, out_required_size)
             : status;
}

canvas_poc04_status_t canvas_poc04_session_surrounding_text_utf8(
    canvas_poc04_handle_t session, uint32_t max_utf16_units, char* buffer,
    size_t buffer_size, size_t* out_required_size) {
  using namespace canvas::poc04;
  std::string value;
  const auto status = Guard([&] {
    value = Utf16ToUtf8(Lookup(session)->SurroundingText(max_utf16_units));
  });
  return status == CANVAS_POC04_STATUS_OK
             ? CopyString(value, buffer, buffer_size, out_required_size)
             : status;
}

canvas_poc04_status_t canvas_poc04_session_presented_utf16_length(
    canvas_poc04_handle_t session, uint64_t* out_length) {
  using namespace canvas::poc04;
  if (out_length == nullptr) return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  return Guard([&] { *out_length = Present(*Lookup(session)).text.size(); });
}

canvas_poc04_status_t canvas_poc04_session_presented_text_range_utf8(
    canvas_poc04_handle_t session, uint64_t location_utf16,
    uint64_t length_utf16, char* buffer, size_t buffer_size,
    size_t* out_required_size) {
  using namespace canvas::poc04;
  std::string value;
  const auto status = Guard([&] {
    const PresentedState state = Present(*Lookup(session));
    ValidateFlatRange(state.text, location_utf16, length_utf16);
    value = Utf16ToUtf8(state.text.substr(location_utf16, length_utf16));
  });
  return status == CANVAS_POC04_STATUS_OK
             ? CopyString(value, buffer, buffer_size, out_required_size)
             : status;
}

canvas_poc04_status_t canvas_poc04_session_replace_range_utf8(
    canvas_poc04_handle_t session, uint64_t location_utf16,
    uint64_t length_utf16, const char* text, size_t text_size) {
  using namespace canvas::poc04;
  if (text == nullptr && text_size != 0) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return Guard([&] {
    TextEditSession* edit = Lookup(session);
    if (edit->composition()) edit->CancelComposition();
    const std::u16string plain = edit->document()->PlainText();
    ValidateFlatRange(plain, location_utf16, length_utf16);
    edit->SetSelection(
        {edit->document()->PositionAtFlatUtf16Offset(location_utf16),
         edit->document()->PositionAtFlatUtf16Offset(location_utf16 + length_utf16)});
    edit->InsertText(Utf8ToUtf16({text == nullptr ? "" : text, text_size}));
  });
}

canvas_poc04_status_t canvas_poc04_session_composition_range_flat_utf16(
    canvas_poc04_handle_t session, canvas_poc04_utf16_range_t* out_range,
    uint32_t* out_active) {
  using namespace canvas::poc04;
  if (out_range == nullptr || out_active == nullptr) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return Guard([&] {
    const PresentedState state = Present(*Lookup(session));
    *out_active = state.marked_range.has_value() ? 1U : 0U;
    *out_range = state.marked_range.value_or(canvas_poc04_utf16_range_t{});
  });
}

canvas_poc04_status_t canvas_poc04_session_composition_selection_utf16(
    canvas_poc04_handle_t session, canvas_poc04_utf16_range_t* out_range,
    uint32_t* out_active) {
  using namespace canvas::poc04;
  if (out_range == nullptr || out_active == nullptr) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return Guard([&] {
    const PresentedState state = Present(*Lookup(session));
    *out_active = state.marked_selection.has_value() ? 1U : 0U;
    *out_range = state.marked_selection.value_or(canvas_poc04_utf16_range_t{});
  });
}

canvas_poc04_status_t canvas_poc04_session_caret_rect_for_offset_utf16(
    canvas_poc04_handle_t session, uint64_t offset_utf16, float layout_width,
    canvas_poc04_rect_t* out_rect) {
  using namespace canvas::poc04;
  if (out_rect == nullptr) return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  return Guard([&] {
    if (!std::isfinite(layout_width) || layout_width <= 0.0F) {
      throw std::invalid_argument("layout width must be finite and positive");
    }
    const PresentedState state = Present(*Lookup(session));
    const FlatGeometry geometry =
        GeometryForOffset(state.text, offset_utf16, layout_width);
    *out_rect = {geometry.x, geometry.y, 1.0F, 20.0F};
  });
}

canvas_poc04_status_t canvas_poc04_session_first_rect_for_range_utf16(
    canvas_poc04_handle_t session, uint64_t location_utf16,
    uint64_t length_utf16, float layout_width, canvas_poc04_rect_t* out_rect,
    canvas_poc04_utf16_range_t* out_actual_range) {
  using namespace canvas::poc04;
  if (out_rect == nullptr || out_actual_range == nullptr) {
    return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  }
  return Guard([&] {
    if (!std::isfinite(layout_width) || layout_width <= 0.0F) {
      throw std::invalid_argument("layout width must be finite and positive");
    }
    const PresentedState state = Present(*Lookup(session));
    ValidateFlatRange(state.text, location_utf16, length_utf16);
    const FlatGeometry start =
        GeometryForOffset(state.text, location_utf16, layout_width);
    uint64_t actual_length = 0;
    float width = 1.0F;
    while (actual_length < length_utf16) {
      const uint64_t next = location_utf16 + actual_length + 1;
      const FlatGeometry end = GeometryForOffset(state.text, next, layout_width);
      if (end.y != start.y) break;
      actual_length += 1;
      width = std::max(1.0F, end.x - start.x);
    }
    *out_rect = {start.x, start.y, width, 20.0F};
    *out_actual_range = {location_utf16, actual_length};
  });
}

canvas_poc04_status_t canvas_poc04_session_character_offset_for_point(
    canvas_poc04_handle_t session, float x, float y, float layout_width,
    uint64_t* out_offset_utf16) {
  using namespace canvas::poc04;
  if (out_offset_utf16 == nullptr) return CANVAS_POC04_STATUS_INVALID_ARGUMENT;
  return Guard([&] {
    const PresentedState state = Present(*Lookup(session));
    *out_offset_utf16 = OffsetForPoint(state.text, x, y, layout_width);
  });
}

canvas_poc04_status_t canvas_poc04_document_digest(
    canvas_poc04_handle_t session, char* buffer, size_t buffer_size,
    size_t* out_required_size) {
  using namespace canvas::poc04;
  std::string value;
  const auto status = Guard([&] { value = Lookup(session)->document()->Digest(); });
  return status == CANVAS_POC04_STATUS_OK
             ? CopyString(value, buffer, buffer_size, out_required_size)
             : status;
}

canvas_poc04_status_t canvas_poc04_operation_log_ndjson(
    canvas_poc04_handle_t session, char* buffer, size_t buffer_size,
    size_t* out_required_size) {
  using namespace canvas::poc04;
  std::string value;
  const auto status = Guard([&] { value = Lookup(session)->OperationLogNdjson(); });
  return status == CANVAS_POC04_STATUS_OK
             ? CopyString(value, buffer, buffer_size, out_required_size)
             : status;
}

const char* canvas_poc04_last_error(void) {
  return canvas::poc04::GetLastError().data();
}

}  // extern "C"
