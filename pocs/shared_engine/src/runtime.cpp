#include "runtime.h"

namespace canvas::poc01 {

Runtime::Runtime(void* user_data, canvas_poc_log_fn log)
    : user_data_(user_data), log_(log) {}

void Runtime::Log(uint32_t level, std::string_view message) const {
  if (log_ != nullptr) {
    log_(user_data_, level, message.data(), message.size());
  }
}

View::View(std::shared_ptr<Document> document, uint32_t width, uint32_t height,
           float device_pixel_ratio)
    : document_(std::move(document)),
      width_(width),
      height_(height),
      device_pixel_ratio_(device_pixel_ratio) {}

canvas_poc_status_t View::Resize(uint32_t width, uint32_t height,
                                 float device_pixel_ratio) {
  if (width == 0 || height == 0 || !IsFinite(device_pixel_ratio) ||
      device_pixel_ratio <= 0.0F || device_pixel_ratio != 1.0F) {
    SetLastError("POC-01 view requires positive dimensions and DPR 1");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  width_ = width;
  height_ = height;
  device_pixel_ratio_ = device_pixel_ratio;
  return CANVAS_POC_STATUS_OK;
}

canvas_poc_status_t View::Render() {
  RuntimeScene scene = compiler_.Compile(*document_);
  return renderer_.Render(scene, document_->assets(), width_, height_,
                          device_pixel_ratio_);
}

}  // namespace canvas::poc01
