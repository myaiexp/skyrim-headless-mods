# CMake toolchain: cross-compile Linux -> x86_64 Windows (MSVC ABI) with clang-cl.
#
# Pairs with plugins/cross-env.sh (must be sourced first so XWIN_SDK is set and
# clang-cl/lld-link/llvm-* are on PATH). Pass to CMake with:
#   -DCMAKE_TOOLCHAIN_FILE=plugins/cmake/clang-cl-msvc.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_CROSSCOMPILING ON)

if(DEFINED ENV{XWIN_SDK})
	set(XWIN_SDK "$ENV{XWIN_SDK}")
endif()
if(NOT EXISTS "${XWIN_SDK}/crt/include")
	message(FATAL_ERROR "clang-cl-msvc.cmake: XWIN_SDK invalid (${XWIN_SDK}). Source plugins/cross-env.sh first.")
endif()

set(_target "x86_64-pc-windows-msvc")

# clang-cl is the MSVC-compatible driver; lld-link links the PE.
set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_C_COMPILER_TARGET   "${_target}")
set(CMAKE_CXX_COMPILER_TARGET "${_target}")
set(CMAKE_LINKER  lld-link)
set(CMAKE_AR      llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT      llvm-mt)

# Don't try to link a full executable while probing the compiler: the CRT is
# present so it would work, but a static lib probe is faster and avoids ordering
# surprises during toolchain detection.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# System include dirs (/imsvc = treat as system, silence their warnings).
set(_inc
	"/imsvc${XWIN_SDK}/crt/include"
	"/imsvc${XWIN_SDK}/sdk/include/ucrt"
	"/imsvc${XWIN_SDK}/sdk/include/um"
	"/imsvc${XWIN_SDK}/sdk/include/shared"
	"/imsvc${XWIN_SDK}/sdk/include/winrt")
string(JOIN " " _inc ${_inc})
string(APPEND CMAKE_C_FLAGS_INIT   " ${_inc}")
string(APPEND CMAKE_CXX_FLAGS_INIT " ${_inc}")

# CommonLibSSE-NG is MSVC-targeted and relies on MSVC's lazy template-body
# checking: some template methods contain typos/missing members but are never
# instantiated, so MSVC never compiles them. clang does conforming current-
# instantiation lookup at parse time and rejects them. -fdelayed-template-parsing
# defers body parsing to instantiation (MSVC semantics) so the dead methods are
# never checked. This is the standard fix for building MSVC C++ with clang-cl.
string(APPEND CMAKE_CXX_FLAGS_INIT " -fdelayed-template-parsing")

# Library search dirs for lld-link.
set(_lib
	"/libpath:${XWIN_SDK}/crt/lib/x86_64"
	"/libpath:${XWIN_SDK}/sdk/lib/ucrt/x86_64"
	"/libpath:${XWIN_SDK}/sdk/lib/um/x86_64")
string(JOIN " " _lib ${_lib})
foreach(_t EXE SHARED MODULE)
	string(APPEND CMAKE_${_t}_LINKER_FLAGS_INIT " ${_lib}")
endforeach()

# Find host programs on the host; find libs/headers/packages in the target sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
