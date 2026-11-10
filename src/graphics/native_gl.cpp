#include "gpu_device.h"
#ifdef XL_STATIC_GRAPHICS
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include <optional>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#define KHRONOS_STATIC 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <mutex>
#include <unordered_map>
#endif
namespace xl::detail {
namespace {
class NativeGL final : public GpuDevice {
#ifdef _WIN32
  HWND window_ = nullptr;
  HDC dc_ = nullptr, previous_dc_ = nullptr;
  HGLRC gl_ = nullptr, previous_gl_ = nullptr;
  HMODULE library_ = nullptr;
  bool saved_ = false;
  static GrGLFuncPtr lookup(void *context, const char *name) {
    auto value = wglGetProcAddress(name);
    if (!value || reinterpret_cast<uintptr_t>(value) <= 3 ||
        reinterpret_cast<intptr_t>(value) == -1)
      value = reinterpret_cast<PROC>(
          GetProcAddress(static_cast<NativeGL *>(context)->library_, name));
    return reinterpret_cast<GrGLFuncPtr>(value);
  }

public:
  bool current() override {
    if (!gl_)
      return false;
    if (!saved_) {
      previous_dc_ = wglGetCurrentDC();
      previous_gl_ = wglGetCurrentContext();
      saved_ = true;
    }
    return wglMakeCurrent(dc_, gl_) != FALSE;
  }
  void release() override {
    if (saved_) {
      wglMakeCurrent(previous_dc_, previous_gl_);
      saved_ = false;
    }
  }
  bool initialize() {
    wchar_t path[32768]{};
    const auto length = GetSystemDirectoryW(path, 32768);
    if (!length || length + 14 >= 32768)
      return false;
    wcscpy_s(path + length, 32768 - length, L"\\opengl32.dll");
    library_ = LoadLibraryW(path);
    if (!library_)
      return false;
    window_ =
        CreateWindowExW(0, L"STATIC", L"XL native OpenGL", WS_POPUP, 0, 0, 1, 1,
                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_ || (dc_ = GetDC(window_)) == nullptr)
      return false;
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.cAlphaBits = 8;
    const auto selected = ChoosePixelFormat(dc_, &format);
    if (!selected || !SetPixelFormat(dc_, selected, &format))
      return false;
    gl_ = wglCreateContext(dc_);
    if (!current())
      return false;
    auto interface = GrGLMakeAssembledGLInterface(this, lookup);
    if (!interface)
      return false;
    context_ = GrDirectContexts::MakeGL(std::move(interface), options());
    return bool(context_);
  }
  ~NativeGL() override {
    if (gl_) {
      if (current())
        context_.reset();
      else {
        abandon();
        context_.reset();
      }
      release();
      wglDeleteContext(gl_);
    }
    if (dc_)
      ReleaseDC(window_, dc_);
    if (window_)
      DestroyWindow(window_);
    if (library_)
      FreeLibrary(library_);
  }
#else
  void *library_ = nullptr;
  void *gl_library_ = nullptr;
  PFNEGLGETPROCADDRESSPROC get_proc_ = nullptr;
  PFNEGLMAKECURRENTPROC make_current_ = nullptr;
  PFNEGLDESTROYCONTEXTPROC destroy_context_ = nullptr;
  PFNEGLDESTROYSURFACEPROC destroy_surface_ = nullptr;
  PFNEGLTERMINATEPROC terminate_ = nullptr;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext gl_ = EGL_NO_CONTEXT;
  EGLSurface surface_ = EGL_NO_SURFACE;
  struct Previous {
    EGLDisplay display;
    EGLContext context;
    EGLSurface draw, read;
    EGLenum api;
  };
  std::optional<Previous> previous_;
  PFNEGLBINDAPIPROC bind_ = nullptr;
  inline static std::mutex displays_mutex_;
  inline static std::unordered_map<EGLDisplay, size_t> displays_;
  bool acquired_ = false;
  template <class T> T proc(const char *name) {
    return reinterpret_cast<T>(dlsym(library_, name));
  }
  static GrGLFuncPtr lookup(void *context, const char *name) {
    auto &self = *static_cast<NativeGL *>(context);
    auto value = self.get_proc_(name);
    if (!value && self.gl_library_)
      value = reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
          dlsym(self.gl_library_, name));
    return reinterpret_cast<GrGLFuncPtr>(value);
  }

public:
  bool current() override {
    if (gl_ == EGL_NO_CONTEXT)
      return false;
    if (!previous_)
      previous_ = Previous{
          proc<PFNEGLGETCURRENTDISPLAYPROC>("eglGetCurrentDisplay")(),
          proc<PFNEGLGETCURRENTCONTEXTPROC>("eglGetCurrentContext")(),
          proc<PFNEGLGETCURRENTSURFACEPROC>("eglGetCurrentSurface")(EGL_DRAW),
          proc<PFNEGLGETCURRENTSURFACEPROC>("eglGetCurrentSurface")(EGL_READ),
          proc<PFNEGLQUERYAPIPROC>("eglQueryAPI")()};
    return bind_(EGL_OPENGL_API) &&
           make_current_(display_, surface_, surface_, gl_);
  }
  void release() override {
    if (!previous_)
      return;
    const auto old = *previous_;
    previous_.reset();
    make_current_(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    bind_(old.api);
    if (old.display != EGL_NO_DISPLAY)
      make_current_(old.display, old.draw, old.read, old.context);
  }
  bool initialize() {
    library_ = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library_)
      return false;
    get_proc_ = proc<PFNEGLGETPROCADDRESSPROC>("eglGetProcAddress");
    const auto get_display = proc<PFNEGLGETDISPLAYPROC>("eglGetDisplay");
    const auto initialize = proc<PFNEGLINITIALIZEPROC>("eglInitialize");
    const auto choose = proc<PFNEGLCHOOSECONFIGPROC>("eglChooseConfig");
    const auto create = proc<PFNEGLCREATECONTEXTPROC>("eglCreateContext");
    const auto pbuffer =
        proc<PFNEGLCREATEPBUFFERSURFACEPROC>("eglCreatePbufferSurface");
    make_current_ = proc<PFNEGLMAKECURRENTPROC>("eglMakeCurrent");
    bind_ = proc<PFNEGLBINDAPIPROC>("eglBindAPI");
    destroy_context_ = proc<PFNEGLDESTROYCONTEXTPROC>("eglDestroyContext");
    destroy_surface_ = proc<PFNEGLDESTROYSURFACEPROC>("eglDestroySurface");
    terminate_ = proc<PFNEGLTERMINATEPROC>("eglTerminate");
    if (!get_proc_ || !get_display || !initialize || !choose || !create ||
        !pbuffer || !make_current_ || !bind_ || !destroy_context_ ||
        !destroy_surface_ || !terminate_)
      return false;
    for (const auto *name : {"eglGetCurrentDisplay", "eglGetCurrentContext",
                             "eglGetCurrentSurface", "eglQueryAPI"})
      if (!dlsym(library_, name))
        return false;
    const auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        get_proc_("eglGetPlatformDisplayEXT"));
    {
      std::lock_guard lock(displays_mutex_);
      if (platform)
        display_ = platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
                            nullptr);
      if (display_ == EGL_NO_DISPLAY)
        display_ = get_display(EGL_DEFAULT_DISPLAY);
      if (display_ == EGL_NO_DISPLAY || !initialize(display_, nullptr, nullptr))
        return false;
      ++displays_[display_];
      acquired_ = true;
    }
    // Selecting the client API is per thread and must also be restored.
    previous_ = Previous{
        proc<PFNEGLGETCURRENTDISPLAYPROC>("eglGetCurrentDisplay")(),
        proc<PFNEGLGETCURRENTCONTEXTPROC>("eglGetCurrentContext")(),
        proc<PFNEGLGETCURRENTSURFACEPROC>("eglGetCurrentSurface")(EGL_DRAW),
        proc<PFNEGLGETCURRENTSURFACEPROC>("eglGetCurrentSurface")(EGL_READ),
        proc<PFNEGLQUERYAPIPROC>("eglQueryAPI")()};
    if (!bind_(EGL_OPENGL_API))
      return false;
    const EGLint attributes[]{EGL_SURFACE_TYPE,
                              EGL_PBUFFER_BIT,
                              EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_BIT,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_ALPHA_SIZE,
                              8,
                              EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    if (!choose(display_, attributes, &config, 1, &count) || !count)
      return false;
    const EGLint size[]{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    surface_ = pbuffer(display_, config, size);
    if (surface_ == EGL_NO_SURFACE)
      return false;
    const EGLint context_attributes[]{EGL_NONE};
    gl_ = create(display_, config, EGL_NO_CONTEXT, context_attributes);
    if (!current())
      return false;
    gl_library_ = dlopen("libOpenGL.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!gl_library_)
      gl_library_ = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
    auto interface = GrGLMakeAssembledGLInterface(this, lookup);
    if (!interface)
      return false;
    context_ = GrDirectContexts::MakeGL(std::move(interface), options());
    return bool(context_);
  }
  ~NativeGL() override {
    if (gl_ != EGL_NO_CONTEXT) {
      if (current())
        context_.reset();
      else {
        abandon();
        context_.reset();
      }
    }
    release();
    if (gl_ != EGL_NO_CONTEXT)
      destroy_context_(display_, gl_);
    if (surface_ != EGL_NO_SURFACE)
      destroy_surface_(display_, surface_);
    if (acquired_) {
      std::lock_guard lock(displays_mutex_);
      if (--displays_.at(display_) == 0) {
        terminate_(display_);
        displays_.erase(display_);
      }
    }
    if (gl_library_)
      dlclose(gl_library_);
    if (library_)
      dlclose(library_);
  }
#endif
};
} // namespace
std::unique_ptr<GpuDevice> make_native_gl() {
  auto device = std::make_unique<NativeGL>();
  if (device->initialize())
    return device;
  return nullptr;
}
} // namespace xl::detail
#endif
