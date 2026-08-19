#ifndef CANVAS_POC03_ANDROID_GLES_ADAPTER_H_
#define CANVAS_POC03_ANDROID_GLES_ADAPTER_H_

#include <android/native_window.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "canvas/poc03/large_scene.h"
#include "canvas/poc03/ink_integration.h"

namespace canvas::poc03 {

class AndroidGlesAdapter {
 public:
  AndroidGlesAdapter();
  ~AndroidGlesAdapter();
  AndroidGlesAdapter(const AndroidGlesAdapter&) = delete;
  AndroidGlesAdapter& operator=(const AndroidGlesAdapter&) = delete;

  bool Attach(ANativeWindow* window, uint32_t width, uint32_t height,
              std::string* error);
  bool Render(const RuntimeScene& scene, const ViewState& view,
              const ViewQueryResult& query, bool readback,
              std::vector<uint8_t>* rgba, double* elapsed_ms,
              std::string* error,
              const InkGeometryStore* ink_geometry = nullptr,
              const poc02::DefaultPreviewSink::State* preview = nullptr);
  void Detach();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::poc03

#endif
