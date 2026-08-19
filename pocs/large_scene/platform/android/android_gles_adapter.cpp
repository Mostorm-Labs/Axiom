#include "android_gles_adapter.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <chrono>
#include <utility>

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "skia_large_scene_renderer.h"

namespace canvas::poc03 {

struct AndroidGlesAdapter::Impl {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext egl_context = EGL_NO_CONTEXT;
  EGLSurface egl_surface = EGL_NO_SURFACE;
  ANativeWindow* window = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  sk_sp<GrDirectContext> context;
  sk_sp<SkSurface> surface;
};

AndroidGlesAdapter::AndroidGlesAdapter() : impl_(std::make_unique<Impl>()) {}
AndroidGlesAdapter::~AndroidGlesAdapter() { Detach(); }

bool AndroidGlesAdapter::Attach(ANativeWindow* window, uint32_t width,
                                uint32_t height, std::string* error) {
  Detach();
  if (window == nullptr || width == 0U || height == 0U) {
    *error = "Android native surface and dimensions are required";
    return false;
  }
  impl_->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (impl_->display == EGL_NO_DISPLAY ||
      eglInitialize(impl_->display, nullptr, nullptr) != EGL_TRUE) {
    *error = "eglInitialize failed";
    return false;
  }
  constexpr EGLint config_attributes[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE,
      EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_STENCIL_SIZE, 8, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint config_count = 0;
  if (eglChooseConfig(impl_->display, config_attributes, &config, 1,
                      &config_count) != EGL_TRUE || config_count != 1) {
    *error = "EGL ES3 config selection failed";
    Detach();
    return false;
  }
  constexpr EGLint context_attributes[] = {
      EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  impl_->egl_context = eglCreateContext(impl_->display, config, EGL_NO_CONTEXT,
                                        context_attributes);
  impl_->egl_surface = eglCreateWindowSurface(impl_->display, config, window,
                                               nullptr);
  if (impl_->egl_context == EGL_NO_CONTEXT ||
      impl_->egl_surface == EGL_NO_SURFACE ||
      eglMakeCurrent(impl_->display, impl_->egl_surface, impl_->egl_surface,
                     impl_->egl_context) != EGL_TRUE) {
    *error = "Android EGL context/surface creation failed";
    Detach();
    return false;
  }
  impl_->context = GrDirectContexts::MakeGL(GrGLMakeNativeInterface());
  if (!impl_->context) {
    *error = "Skia Ganesh GLES context creation failed";
    Detach();
    return false;
  }
  GLint framebuffer_id = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_id);
  GrGLFramebufferInfo framebuffer{};
  framebuffer.fFBOID = static_cast<GrGLuint>(framebuffer_id);
  framebuffer.fFormat = static_cast<GrGLenum>(GL_RGBA8);
  const GrBackendRenderTarget target = GrBackendRenderTargets::MakeGL(
      static_cast<int>(width), static_cast<int>(height), 1, 8, framebuffer);
  impl_->surface = SkSurfaces::WrapBackendRenderTarget(
      impl_->context.get(), target, kBottomLeft_GrSurfaceOrigin,
      kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
  if (!impl_->surface) {
    *error = "Skia could not wrap Android default framebuffer";
    Detach();
    return false;
  }
  impl_->window = window;
  ANativeWindow_acquire(window);
  impl_->width = width;
  impl_->height = height;
  // The validation and interactive lanes may render on different host
  // threads. Do not leave ownership of the EGL context on the attach thread.
  eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 EGL_NO_CONTEXT);
  return true;
}

bool AndroidGlesAdapter::Render(const RuntimeScene& scene,
                                const ViewState& view,
                                const ViewQueryResult& query, bool readback,
                                std::vector<uint8_t>* rgba,
                                double* elapsed_ms, std::string* error,
                                const InkGeometryStore* ink_geometry,
                                const poc02::DefaultPreviewSink::State* preview) {
  if (!impl_->surface || eglMakeCurrent(impl_->display, impl_->egl_surface,
      impl_->egl_surface, impl_->egl_context) != EGL_TRUE) {
    *error = "Android GLES surface is detached";
    return false;
  }
  const auto start = std::chrono::steady_clock::now();
  DrawLargeScene(*impl_->surface->getCanvas(), scene, view,
                 BuildFrame(scene, query, {}), ink_geometry, preview);
  impl_->context->flushAndSubmit(
      impl_->surface.get(), readback ? GrSyncCpu::kYes : GrSyncCpu::kNo);
  if (readback && !ReadRgba(*impl_->surface, impl_->width, impl_->height, rgba)) {
    *error = "Android RGBA readback failed";
    return false;
  }
  if (eglSwapBuffers(impl_->display, impl_->egl_surface) != EGL_TRUE) {
    *error = "eglSwapBuffers failed";
    return false;
  }
  *elapsed_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 EGL_NO_CONTEXT);
  return true;
}

void AndroidGlesAdapter::Detach() {
  if (!impl_) return;
  impl_->surface.reset();
  if (impl_->context) {
    impl_->context->abandonContext();
    impl_->context.reset();
  }
  if (impl_->display != EGL_NO_DISPLAY) {
    eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (impl_->egl_surface != EGL_NO_SURFACE) {
      eglDestroySurface(impl_->display, impl_->egl_surface);
    }
    if (impl_->egl_context != EGL_NO_CONTEXT) {
      eglDestroyContext(impl_->display, impl_->egl_context);
    }
    eglTerminate(impl_->display);
  }
  if (impl_->window != nullptr) ANativeWindow_release(impl_->window);
  *impl_ = Impl{};
}

}  // namespace canvas::poc03
