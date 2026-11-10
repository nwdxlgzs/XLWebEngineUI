#define KHRONOS_STATIC 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cstdio>
int main(){
 auto platform=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
 if(!platform)return 1;
 const EGLint attr[]={EGL_PLATFORM_ANGLE_TYPE_ANGLE,
#ifdef _WIN32
 EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
#else
 EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE,
#endif
 EGL_NONE};
 auto display=platform(EGL_PLATFORM_ANGLE_ANGLE,EGL_DEFAULT_DISPLAY,attr);
 EGLint major=0,minor=0;
 if(!eglInitialize(display,&major,&minor)){
  std::printf("eglInitialize error %x\n",eglGetError());
#ifdef _WIN32
  return 2;
#else
  // A musl SDK cannot use the build host's glibc-only graphics driver.
  // Report unavailable hardware separately; SwiftShader must still draw.
  return 77;
#endif
 }
 const EGLint wanted[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
 EGLConfig config{};EGLint count=0;if(!eglChooseConfig(display,wanted,&config,1,&count)||!count)return 3;
 const EGLint size[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE};auto surface=eglCreatePbufferSurface(display,config,size);
 const EGLint version[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);
 if(surface==EGL_NO_SURFACE||context==EGL_NO_CONTEXT||!eglMakeCurrent(display,surface,surface,context))return 4;
 glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);unsigned char pixel[4]={};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
 std::printf("EGL %d.%d renderer=%s pixel=%u,%u,%u,%u\n",major,minor,glGetString(GL_RENDERER),pixel[0],pixel[1],pixel[2],pixel[3]);
 const auto good=glGetError()==GL_NO_ERROR&&pixel[0]==255&&pixel[1]==0&&pixel[2]==0&&pixel[3]==255;
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);return good?0:5;
}
