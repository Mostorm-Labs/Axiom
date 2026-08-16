#ifndef CANVAS_POC_APPLE_METAL_ADAPTER_H_
#define CANVAS_POC_APPLE_METAL_ADAPTER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "document.h"

namespace canvas::poc01 {

struct AppleMetalResult {
  std::string device_name;
  std::vector<uint8_t> rgba;
};

class AppleMetalAdapter {
 public:
  AppleMetalAdapter();
  ~AppleMetalAdapter();
  AppleMetalAdapter(const AppleMetalAdapter&) = delete;
  AppleMetalAdapter& operator=(const AppleMetalAdapter&) = delete;

  canvas_poc_status_t Initialize(uint32_t width, uint32_t height);
  canvas_poc_status_t Render(const Document& document,
                             std::vector<uint8_t>* readback);
  const std::string& device_name() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

canvas_poc_status_t RenderAppleMetal(const Document& document,
                                     AppleMetalResult* result);

}  // namespace canvas::poc01

#endif  // CANVAS_POC_APPLE_METAL_ADAPTER_H_
