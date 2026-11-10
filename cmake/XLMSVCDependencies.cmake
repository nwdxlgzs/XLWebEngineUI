# Some Windows installations emit UTF-8 diagnostics while older CMake decodes
# compiler identification through the ANSI codepage. Probe the actual prefix so
# Ninja records header dependencies instead of silently printing/ignoring them.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
  file(WRITE "${CMAKE_BINARY_DIR}/CMakeFiles/xl-includes-probe.cpp" "#include <stddef.h>\nint xl_probe;\n")
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c
      "${CMAKE_BINARY_DIR}/CMakeFiles/xl-includes-probe.cpp"
      "/Fo${CMAKE_BINARY_DIR}/CMakeFiles/xl-includes-probe.obj"
    OUTPUT_VARIABLE xl_probe_output ERROR_VARIABLE xl_probe_error
    RESULT_VARIABLE xl_probe_result ENCODING UTF-8)
  if(xl_probe_result EQUAL 0)
    string(REGEX MATCH "(^|\n)([^\n]+:) +[A-Za-z]:" xl_prefix_match "${xl_probe_output}\n${xl_probe_error}")
    if(xl_prefix_match)
      set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2} ")
    endif()
  endif()
endif()
