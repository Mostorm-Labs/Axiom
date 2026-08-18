#include "android_gles_adapter.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <utility>

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "ink_skia_renderer.h"

namespace canvas::poc02 {

struct AndroidGlesAdapter::Impl {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext egl_context = EGL_NO_CONTEXT;
  EGLSurface egl_surface = EGL_NO_SURFACE;
  ANativeWindow* window = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  sk_sp<GrDirectContext> gr_context;
  sk_sp<SkSurface> sk_surface;
  InkSkiaRenderer renderer;
  std::string error;
};

AndroidGlesAdapter::AndroidGlesAdapter() : impl_(std::make_unique<Impl>()) {}
AndroidGlesAdapter::~AndroidGlesAdapter() { Detach(); }

Status AndroidGlesAdapter::Attach(ANativeWindow* window, uint32_t width,
                                  uint32_t height) {
  Detach();
  if (window == nullptr || width == 0 || height == 0) {
    impl_->error = "Android surface and dimensions must be valid";
    return Status::kInvalidArgument;
  }
  impl_->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (impl_->display == EGL_NO_DISPLAY ||
      eglInitialize(impl_->display, nullptr, nullptr) != EGL_TRUE) {
    impl_->error = "eglInitialize failed";
    return Status::kInvalidState;
  }
  constexpr EGLint config_attributes[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE,
      EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8, EGL_STENCIL_SIZE, 8, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (eglChooseConfig(impl_->display, config_attributes, &config, 1, &count) !=
          EGL_TRUE || count != 1) {
    impl_->error = "EGL ES3 config selection failed";
    Detach();
    return Status::kInvalidState;
  }
  constexpr EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                            EGL_NONE};
  impl_->egl_context = eglCreateContext(impl_->display, config, EGL_NO_CONTEXT,
                                        context_attributes);
  impl_->egl_surface = eglCreateWindowSurface(impl_->display, config, window, nullptr);
  if (impl_->egl_context == EGL_NO_CONTEXT ||
      impl_->egl_surface == EGL_NO_SURFACE ||
      eglMakeCurrent(impl_->display, impl_->egl_surface, impl_->egl_surface,
                     impl_->egl_context) != EGL_TRUE) {
    impl_->error = "Android EGL context/surface creation failed";
    Detach();
    return Status::kInvalidState;
  }
  impl_->gr_context = GrDirectContexts::MakeGL(GrGLMakeNativeInterface());
  if (impl_->gr_context == nullptr) {
    impl_->error = "Skia failed to create Android Ganesh GLES context";
    Detach();
    return Status::kInvalidState;
  }
  GLint framebuffer_id = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_id);
  GrGLFramebufferInfo framebuffer{static_cast<GrGLuint>(framebuffer_id),
                                  static_cast<GrGLenum>(GL_RGBA8)};
  GrBackendRenderTarget target = GrBackendRenderTargets::MakeGL(
      static_cast<int>(width), static_cast<int>(height), 1, 8, framebuffer);
  impl_->sk_surface = SkSurfaces::WrapBackendRenderTarget(
      impl_->gr_context.get(), target, kBottomLeft_GrSurfaceOrigin,
      kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
  if (impl_->sk_surface == nullptr) {
    impl_->error = "Skia failed to wrap Android framebuffer";
    Detach();
    return Status::kInvalidState;
  }
  impl_->window = window;
  ANativeWindow_acquire(window);
  impl_->width = width;
  impl_->height = height;
  return Status::kOk;
}

Status AndroidGlesAdapter::Render(
    const StrokeDocument& document, const DefaultPreviewSink::State* preview,
    std::vector<uint8_t>* readback) {
  if (impl_->sk_surface == nullptr ||
      eglMakeCurrent(impl_->display, impl_->egl_surface, impl_->egl_surface,
                     impl_->egl_context) != EGL_TRUE) {
    impl_->error = "Android surface is not attached";
    return Status::kInvalidState;
  }
  impl_->renderer.Draw(*impl_->sk_surface->getCanvas(), impl_->width,
                       impl_->height, document, preview);
  impl_->gr_context->flushAndSubmit(
      impl_->sk_surface.get(), readback == nullptr ? GrSyncCpu::kNo
                                                   : GrSyncCpu::kYes);
  if (readback != nullptr && !impl_->renderer.Readback(
                                  *impl_->sk_surface, impl_->width,
                                  impl_->height, readback)) {
    impl_->error = "Android GLES RGBA readback failed";
    return Status::kInvalidState;
  }
  if (eglSwapBuffers(impl_->display, impl_->egl_surface) != EGL_TRUE) {
    impl_->error = "eglSwapBuffers failed";
    return Status::kInvalidState;
  }
  return Status::kOk;
}

void AndroidGlesAdapter::Detach() {
  if (!impl_) return;
  impl_->sk_surface.reset();
  if (impl_->gr_context != nullptr) {
    impl_->gr_context->abandonContext();
    impl_->gr_context.reset();
  }
  if (impl_->display != EGL_NO_DISPLAY) {
    eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (impl_->egl_surface != EGL_NO_SURFACE)
      eglDestroySurface(impl_->display, impl_->egl_surface);
    if (impl_->egl_context != EGL_NO_CONTEXT)
      eglDestroyContext(impl_->display, impl_->egl_context);
    eglTerminate(impl_->display);
  }
  if (impl_->window != nullptr) ANativeWindow_release(impl_->window);
  impl_->display = EGL_NO_DISPLAY;
  impl_->egl_context = EGL_NO_CONTEXT;
  impl_->egl_surface = EGL_NO_SURFACE;
  impl_->window = nullptr;
  impl_->width = 0;
  impl_->height = 0;
}

const std::string& AndroidGlesAdapter::error() const { return impl_->error; }

}  // namespace canvas::poc02
