#ifndef CANVAS_POC_ANDROID_GLES_ADAPTER_H_
#define CANVAS_POC_ANDROID_GLES_ADAPTER_H_

#include <android/native_window.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "document.h"

namespace canvas::poc01 {

class AndroidGlesAdapter {
 public:
  AndroidGlesAdapter();
  ~AndroidGlesAdapter();
  AndroidGlesAdapter(const AndroidGlesAdapter&) = delete;
  AndroidGlesAdapter& operator=(const AndroidGlesAdapter&) = delete;

  canvas_poc_status_t Attach(ANativeWindow* window, uint32_t width,
                             uint32_t height);
  canvas_poc_status_t Render(const Document& document,
                             std::vector<uint8_t>* readback);
  void Detach();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::poc01

#endif  // CANVAS_POC_ANDROID_GLES_ADAPTER_H_
