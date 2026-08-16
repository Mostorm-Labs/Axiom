#include "canvas_poc/canvas_poc.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "document.h"
#include "foundation.h"
#include "operations.h"
#include "platform_bridge_internal.h"
#include "runtime.h"

namespace canvas::poc01 {
namespace {

template <typename T>
class HandleRegistry {
 public:
  canvas_poc_status_t Insert(std::shared_ptr<T> value,
                             canvas_poc_handle_t* out_handle) {
    if (out_handle == nullptr) {
      SetLastError("out_handle must not be null");
      return CANVAS_POC_STATUS_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < slots_.size(); ++index) {
      Slot& slot = slots_[index];
      if (slot.value == nullptr) {
        slot.value = std::move(value);
        *out_handle = Encode(index, slot.generation);
        return CANVAS_POC_STATUS_OK;
      }
    }
    if (slots_.size() >= kMaxSlots) {
      SetLastError("generation handle registry is full");
      return CANVAS_POC_STATUS_INTERNAL_ERROR;
    }
    Slot slot;
    slot.value = std::move(value);
    slots_.push_back(std::move(slot));
    *out_handle = Encode(slots_.size() - 1, slots_.back().generation);
    return CANVAS_POC_STATUS_OK;
  }

  std::shared_ptr<T> Find(canvas_poc_handle_t handle) const {
    size_t index = 0;
    uint16_t generation = 0;
    if (!Decode(handle, &index, &generation) || index >= slots_.size()) {
      return nullptr;
    }
    const Slot& slot = slots_[index];
    return slot.generation == generation ? slot.value : nullptr;
  }

  canvas_poc_status_t Remove(canvas_poc_handle_t handle) {
    size_t index = 0;
    uint16_t generation = 0;
    if (!Decode(handle, &index, &generation) || index >= slots_.size()) {
      SetLastError("invalid or stale generation handle");
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    Slot& slot = slots_[index];
    if (slot.value == nullptr || slot.generation != generation) {
      SetLastError("invalid or stale generation handle");
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    slot.value.reset();
    ++slot.generation;
    if (slot.generation == 0) {
      slot.generation = 1;
    }
    return CANVAS_POC_STATUS_OK;
  }

 private:
  static constexpr size_t kMaxSlots = 65535;

  struct Slot {
    uint16_t generation = 1;
    std::shared_ptr<T> value;
  };

  static canvas_poc_handle_t Encode(size_t index, uint16_t generation) {
    return (static_cast<uint32_t>(generation) << 16U) |
           static_cast<uint32_t>(index + 1U);
  }

  static bool Decode(canvas_poc_handle_t handle, size_t* out_index,
                     uint16_t* out_generation) {
    const uint16_t encoded_index = static_cast<uint16_t>(handle & 0xffffU);
    const uint16_t generation = static_cast<uint16_t>(handle >> 16U);
    if (encoded_index == 0 || generation == 0) {
      return false;
    }
    *out_index = static_cast<size_t>(encoded_index - 1U);
    *out_generation = generation;
    return true;
  }

  std::vector<Slot> slots_;
};

HandleRegistry<Runtime> g_runtimes;
HandleRegistry<Document> g_documents;
HandleRegistry<View> g_views;

template <typename Function>
canvas_poc_status_t Boundary(Function&& function, bool clear_error = true) {
  if (clear_error) {
    ClearLastError();
  }
  try {
    return std::forward<Function>(function)();
  } catch (const std::bad_alloc&) {
    SetLastError("allocation failed");
    return CANVAS_POC_STATUS_INTERNAL_ERROR;
  } catch (const std::exception& error) {
    SetLastError(std::string("C++ exception stopped at ABI boundary: ") +
                 error.what());
    return CANVAS_POC_STATUS_INTERNAL_ERROR;
  } catch (...) {
    SetLastError("unknown exception stopped at ABI boundary");
    return CANVAS_POC_STATUS_INTERNAL_ERROR;
  }
}

template <typename T>
canvas_poc_status_t ValidateConfig(const T* config) {
  if (config == nullptr) {
    SetLastError("configuration must not be null");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  if (config->struct_size < sizeof(T)) {
    SetLastError("configuration struct_size is too small");
    return CANVAS_POC_STATUS_ABI_MISMATCH;
  }
  if (config->abi_version != CANVAS_POC_ABI_VERSION) {
    SetLastError("configuration abi_version is unsupported");
    return CANVAS_POC_STATUS_ABI_MISMATCH;
  }
  return CANVAS_POC_STATUS_OK;
}

template <typename T>
std::shared_ptr<T> Require(HandleRegistry<T>& registry,
                           canvas_poc_handle_t handle) {
  std::shared_ptr<T> value = registry.Find(handle);
  if (value == nullptr) {
    SetLastError("invalid or stale generation handle");
  }
  return value;
}

}  // namespace

std::shared_ptr<Document> ResolveDocumentForPlatform(
    canvas_poc_handle_t document) {
  return g_documents.Find(document);
}

}  // namespace canvas::poc01

using canvas::poc01::Boundary;

extern "C" {

canvas_poc_status_t canvas_poc_runtime_create(
    const canvas_poc_runtime_config_v1* config,
    canvas_poc_handle_t* out_runtime) {
  return Boundary([&] {
    const canvas_poc_status_t validation =
        canvas::poc01::ValidateConfig(config);
    if (validation != CANVAS_POC_STATUS_OK) {
      return validation;
    }
    return canvas::poc01::g_runtimes.Insert(
        std::make_shared<canvas::poc01::Runtime>(config->user_data,
                                                 config->log),
        out_runtime);
  });
}

canvas_poc_status_t canvas_poc_runtime_destroy(canvas_poc_handle_t runtime) {
  return Boundary(
      [&] { return canvas::poc01::g_runtimes.Remove(runtime); });
}

canvas_poc_status_t canvas_poc_runtime_register_asset(
    canvas_poc_handle_t runtime, const char* key, size_t key_size,
    const uint8_t* bytes, size_t byte_count) {
  return Boundary([&] {
    const auto value = canvas::poc01::Require(canvas::poc01::g_runtimes,
                                               runtime);
    if (value == nullptr) {
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    if (key == nullptr || bytes == nullptr || key_size == 0 ||
        byte_count == 0) {
      canvas::poc01::SetLastError("asset key and bytes must be non-empty");
      return CANVAS_POC_STATUS_INVALID_ARGUMENT;
    }
    return value->assets()->Register(
        std::string(key, key_size),
        std::span<const uint8_t>(bytes, byte_count));
  });
}

canvas_poc_status_t canvas_poc_document_create(
    canvas_poc_handle_t runtime, const canvas_poc_document_config_v1* config,
    canvas_poc_handle_t* out_document) {
  return Boundary([&] {
    const canvas_poc_status_t validation =
        canvas::poc01::ValidateConfig(config);
    if (validation != CANVAS_POC_STATUS_OK) {
      return validation;
    }
    const auto runtime_value = canvas::poc01::Require(
        canvas::poc01::g_runtimes, runtime);
    if (runtime_value == nullptr) {
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    if (config->page_width == 0 || config->page_height == 0) {
      canvas::poc01::SetLastError("page dimensions must be positive");
      return CANVAS_POC_STATUS_INVALID_ARGUMENT;
    }
    const canvas::poc01::Color background{
        config->background_rgba[0], config->background_rgba[1],
        config->background_rgba[2], config->background_rgba[3]};
    return canvas::poc01::g_documents.Insert(
        std::make_shared<canvas::poc01::Document>(
            runtime_value->assets(), config->page_width, config->page_height,
            background),
        out_document);
  });
}

canvas_poc_status_t canvas_poc_document_destroy(
    canvas_poc_handle_t document) {
  return Boundary(
      [&] { return canvas::poc01::g_documents.Remove(document); });
}

canvas_poc_status_t canvas_poc_document_apply_ndjson(
    canvas_poc_handle_t document, const char* ndjson, size_t ndjson_size) {
  return Boundary([&] {
    const auto value = canvas::poc01::Require(canvas::poc01::g_documents,
                                               document);
    if (value == nullptr) {
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    if (ndjson == nullptr || ndjson_size == 0) {
      canvas::poc01::SetLastError("NDJSON bytes must be non-empty");
      return CANVAS_POC_STATUS_INVALID_ARGUMENT;
    }
    return canvas::poc01::ApplyOperations(
        *value, std::string_view(ndjson, ndjson_size));
  });
}

canvas_poc_status_t canvas_poc_document_digest(
    canvas_poc_handle_t document, char* buffer, size_t buffer_size,
    size_t* out_required_size) {
  return Boundary([&] {
    const auto value = canvas::poc01::Require(canvas::poc01::g_documents,
                                               document);
    if (value == nullptr) {
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    return canvas::poc01::CopyToCaller(value->Digest(), buffer, buffer_size,
                                       out_required_size, true);
  });
}

canvas_poc_status_t canvas_poc_document_sequence(
    canvas_poc_handle_t document, uint64_t* out_sequence) {
  return Boundary([&] {
    const auto value = canvas::poc01::Require(canvas::poc01::g_documents,
                                               document);
    if (value == nullptr) {
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    if (out_sequence == nullptr) {
      canvas::poc01::SetLastError("out_sequence must not be null");
      return CANVAS_POC_STATUS_INVALID_ARGUMENT;
    }
    *out_sequence = value->state().last_sequence;
    return CANVAS_POC_STATUS_OK;
  });
}

canvas_poc_status_t canvas_poc_view_create_offscreen(
    canvas_poc_handle_t document, const canvas_poc_view_config_v1* config,
    canvas_poc_handle_t* out_view) {
  return Boundary([&] {
    const canvas_poc_status_t validation =
        canvas::poc01::ValidateConfig(config);
    if (validation != CANVAS_POC_STATUS_OK) {
      return validation;
    }
    const auto document_value = canvas::poc01::Require(
        canvas::poc01::g_documents, document);
    if (document_value == nullptr) {
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    if (config->width == 0 || config->height == 0 ||
        !canvas::poc01::IsFinite(config->device_pixel_ratio) ||
        config->device_pixel_ratio != 1.0F) {
      canvas::poc01::SetLastError(
          "POC-01 offscreen view requires positive dimensions and DPR 1");
      return CANVAS_POC_STATUS_INVALID_ARGUMENT;
    }
    return canvas::poc01::g_views.Insert(
        std::make_shared<canvas::poc01::View>(
            document_value, config->width, config->height,
            config->device_pixel_ratio),
        out_view);
  });
}

canvas_poc_status_t canvas_poc_view_destroy(canvas_poc_handle_t view) {
  return Boundary([&] { return canvas::poc01::g_views.Remove(view); });
}

canvas_poc_status_t canvas_poc_view_resize(canvas_poc_handle_t view,
                                            uint32_t width, uint32_t height,
                                            float device_pixel_ratio) {
  return Boundary([&] {
    const auto value =
        canvas::poc01::Require(canvas::poc01::g_views, view);
    return value == nullptr
               ? CANVAS_POC_STATUS_INVALID_HANDLE
               : value->Resize(width, height, device_pixel_ratio);
  });
}

canvas_poc_status_t canvas_poc_view_render(canvas_poc_handle_t view) {
  return Boundary([&] {
    const auto value =
        canvas::poc01::Require(canvas::poc01::g_views, view);
    return value == nullptr ? CANVAS_POC_STATUS_INVALID_HANDLE
                            : value->Render();
  });
}

canvas_poc_status_t canvas_poc_view_read_rgba(
    canvas_poc_handle_t view, uint8_t* buffer, size_t buffer_size,
    size_t* out_required_size) {
  return Boundary([&] {
    const auto value =
        canvas::poc01::Require(canvas::poc01::g_views, view);
    if (value == nullptr) {
      return CANVAS_POC_STATUS_INVALID_HANDLE;
    }
    return canvas::poc01::CopyToCaller(value->pixels(), buffer, buffer_size,
                                       out_required_size);
  });
}

const char* canvas_poc_status_message(canvas_poc_status_t status) {
  switch (status) {
    case CANVAS_POC_STATUS_OK:
      return "ok";
    case CANVAS_POC_STATUS_INVALID_ARGUMENT:
      return "invalid argument";
    case CANVAS_POC_STATUS_ABI_MISMATCH:
      return "ABI mismatch";
    case CANVAS_POC_STATUS_INVALID_HANDLE:
      return "invalid handle";
    case CANVAS_POC_STATUS_NOT_FOUND:
      return "not found";
    case CANVAS_POC_STATUS_ALREADY_EXISTS:
      return "already exists";
    case CANVAS_POC_STATUS_BUFFER_TOO_SMALL:
      return "buffer too small";
    case CANVAS_POC_STATUS_PARSE_ERROR:
      return "parse error";
    case CANVAS_POC_STATUS_SEQUENCE_ERROR:
      return "sequence error";
    case CANVAS_POC_STATUS_ASSET_ERROR:
      return "asset error";
    case CANVAS_POC_STATUS_RENDER_ERROR:
      return "render error";
    case CANVAS_POC_STATUS_PLATFORM_ERROR:
      return "platform error";
    case CANVAS_POC_STATUS_INTERNAL_ERROR:
      return "internal error";
  }
  return "unknown status";
}

canvas_poc_status_t canvas_poc_last_error(char* buffer, size_t buffer_size,
                                           size_t* out_required_size) {
  return Boundary(
      [&] {
        return canvas::poc01::CopyToCaller(canvas::poc01::GetLastError(),
                                           buffer, buffer_size,
                                           out_required_size, true);
      },
      false);
}

}  // extern "C"
