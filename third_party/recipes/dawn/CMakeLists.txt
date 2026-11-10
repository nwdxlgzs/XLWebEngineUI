cmake_minimum_required(VERSION 3.25)
project(XLDawn LANGUAGES C CXX)
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT XL_DAWN_SOURCE OR NOT EXISTS "${XL_DAWN_SOURCE}/CMakeLists.txt")
  message(FATAL_ERROR "A locked Dawn overlay is required")
endif()
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 WINVER=0x0601)
  set(XL_DAWN_STATIC_DXC ON)
  include_directories("${XL_DAWN_DXC_INCLUDE}/dxc")
endif()
set(XL_DAWN_STATIC_SWIFTSHADER ON)
# Dawn consumes only this upstream header interface. Its standalone top-level
# project also searches for an installed Vulkan SDK and configures unrelated
# layer/settings libraries; the exact locked headers are already available.
if(DAWN_ENABLE_VULKAN)
  add_library(xl_dawn_vulkan_utility_headers INTERFACE)
  target_include_directories(xl_dawn_vulkan_utility_headers INTERFACE "${DAWN_VULKAN_UTILITY_LIBRARIES_DIR}/include")
  target_link_libraries(xl_dawn_vulkan_utility_headers INTERFACE Vulkan::Headers)
  add_library(Vulkan::UtilityHeaders ALIAS xl_dawn_vulkan_utility_headers)
endif()
add_subdirectory("${XL_DAWN_SOURCE}" dawn)

# Reuse upstream's object traversal, including the WGSL writer needed by Skia.
# Existing isolated DXC/SwiftShader/ANGLE are linked by our final consumer and
# never copied into a second Dawn dependency archive.
bundle_libraries(xl_dawn STATIC webgpu_dawn_objects tint_lang_wgsl_writer)
target_link_libraries(xl_dawn PUBLIC dawn_public_config)
target_include_directories(xl_dawn PUBLIC "${XL_DAWN_SOURCE}")
if(XL_DAWN_LLVM_TLS_SOURCE)
  # Compile the locked upstream fallback without HAVE___CXA_THREAD_ATEXIT_IMPL.
  # The libc++ archive's alternate definition is then never extracted. Keep
  # the standard ABI symbol; the final shared library exports only xl_*.
  set(XL_DAWN_TLS_SOURCE "${XL_DAWN_LLVM_TLS_SOURCE}/libcxxabi/src/cxa_thread_atexit.cpp")
  target_sources(xl_dawn PRIVATE "${XL_DAWN_TLS_SOURCE}")
  set_source_files_properties("${XL_DAWN_TLS_SOURCE}" PROPERTIES COMPILE_DEFINITIONS "_LIBCXXABI_BUILDING_LIBRARY")
  target_include_directories(xl_dawn PRIVATE "${XL_DAWN_LLVM_TLS_SOURCE}/libcxxabi/include"
    "${XL_DAWN_LLVM_TLS_SOURCE}/libcxx/src")
endif()
if(WIN32)
  target_link_libraries(xl_dawn PRIVATE dxguid)
endif()

if(XL_DAWN_PROBE)
  add_executable(xl_dawn_probe "${XL_DAWN_PROBE}")
  target_include_directories(xl_dawn_probe PRIVATE "${DAWN_EGL_REGISTRY_DIR}/api")
  target_include_directories(xl_dawn_probe PRIVATE "${XL_DAWN_ANGLE_INCLUDE}")
  target_link_libraries(xl_dawn_probe PRIVATE xl_dawn "${XL_DAWN_SWIFTSHADER_LIBRARY}" "${XL_DAWN_ANGLE_LIBRARY}" ${XL_DAWN_PROBE_LIBRARIES})
  if(WIN32)
    target_link_libraries(xl_dawn_probe PRIVATE "${XL_DAWN_DXC_LIBRARY}" dxgi dxguid advapi32 user32 gdi32 opengl32)
  endif()
endif()
