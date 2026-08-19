#include "canvas_poc04/canvas_poc04.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>

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
