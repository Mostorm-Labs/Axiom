#ifndef CANVAS_POC02_ANDROID_GLES_ADAPTER_H_
#define CANVAS_POC02_ANDROID_GLES_ADAPTER_H_

#include <android/native_window.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "canvas_poc02/ink_engine.h"

namespace canvas::poc02 {

class AndroidGlesAdapter {
 public:
  AndroidGlesAdapter();
  ~AndroidGlesAdapter();
  AndroidGlesAdapter(const AndroidGlesAdapter&) = delete;
  AndroidGlesAdapter& operator=(const AndroidGlesAdapter&) = delete;

  Status Attach(ANativeWindow* window, uint32_t width, uint32_t height);
  Status Render(const StrokeDocument& document,
                const DefaultPreviewSink::State* preview,
                std::vector<uint8_t>* readback = nullptr);
  void Detach();
  [[nodiscard]] const std::string& error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::poc02

#endif  // CANVAS_POC02_ANDROID_GLES_ADAPTER_H_
