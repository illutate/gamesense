set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(SKEET_LLVM_MINGW_ROOT "$ENV{SKEET_LLVM_MINGW_ROOT}" CACHE PATH
  "LLVM-MinGW root containing the i686 Windows toolchain and sysroot")
if(NOT SKEET_LLVM_MINGW_ROOT)
  message(FATAL_ERROR
    "Set SKEET_LLVM_MINGW_ROOT to an LLVM-MinGW installation, either as an "
    "environment variable or with -DSKEET_LLVM_MINGW_ROOT=<path>.")
endif()
get_filename_component(SKEET_LLVM_MINGW_ROOT "${SKEET_LLVM_MINGW_ROOT}" ABSOLUTE)

set(_skeet_toolchain_bin "${SKEET_LLVM_MINGW_ROOT}/bin")
set(CMAKE_C_COMPILER "${_skeet_toolchain_bin}/i686-w64-mingw32-clang.exe")
set(CMAKE_CXX_COMPILER "${_skeet_toolchain_bin}/i686-w64-mingw32-clang++.exe")
set(CMAKE_RC_COMPILER "${_skeet_toolchain_bin}/i686-w64-mingw32-windres.exe")
set(CMAKE_AR "${_skeet_toolchain_bin}/llvm-ar.exe")
set(CMAKE_RANLIB "${_skeet_toolchain_bin}/llvm-ranlib.exe")
set(CMAKE_C_COMPILER_TARGET i686-w64-windows-gnu)
set(CMAKE_CXX_COMPILER_TARGET i686-w64-windows-gnu)

foreach(_skeet_required_tool IN ITEMS CMAKE_C_COMPILER CMAKE_CXX_COMPILER
    CMAKE_RC_COMPILER CMAKE_AR CMAKE_RANLIB)
  if(NOT EXISTS "${${_skeet_required_tool}}")
    message(FATAL_ERROR "Missing required x86 tool: ${${_skeet_required_tool}}")
  endif()
endforeach()

set(CMAKE_C_FLAGS_INIT "-m32 -msse2 -mfpmath=sse -ffp-contract=off -fno-fast-math")
set(CMAKE_CXX_FLAGS_INIT "-m32 -msse2 -mfpmath=sse -ffp-contract=off -fno-fast-math")

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES SKEET_LLVM_MINGW_ROOT)

unset(_skeet_required_tool)
unset(_skeet_toolchain_bin)
