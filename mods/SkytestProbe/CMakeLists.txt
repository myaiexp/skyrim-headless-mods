cmake_minimum_required(VERSION 3.24)
project(SkytestProbe VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

# --- spdlog: satisfies CommonLibSSE-NG's find_package(spdlog CONFIG REQUIRED).
# OVERRIDE_FIND_PACKAGE redirects that find_package() to this built target.
# Floor is NG's own vcpkg.json (spdlog >= 1.16.0, i.e. fmt >= 12.1.0) — the old
# v1.13.0/fmt-10 pin no longer compiles against it.
FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog
	GIT_TAG v1.16.0
	OVERRIDE_FIND_PACKAGE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
# NG's install(EXPORT) references spdlog, so spdlog must also be in an export
# set — enabling its install target puts it there (we never actually install).
set(SPDLOG_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# --- rapidcsv: header-only. NG locates it via find_path(rapidcsv.h); pre-seed
# the cache var it checks so no system lookup is needed. (We don't use rapidcsv
# directly, but NG's CMake requires it be discoverable.)
FetchContent_Declare(rapidcsv
	GIT_REPOSITORY https://github.com/d99kris/rapidcsv
	GIT_TAG v8.97)
FetchContent_MakeAvailable(rapidcsv)
set(RAPIDCSV_INCLUDE_DIRS "${rapidcsv_SOURCE_DIR}/src" CACHE PATH "" FORCE)

# --- DirectXTK: NG does find_package(directxtk CONFIG REQUIRED) and links
# Microsoft::DirectXTK for exactly one thing — <SimpleMath.h> in RE/S/State.h,
# and only for FIELD TYPES (Vector4/Matrix in the ViewData layout struct). No
# SimpleMath function or static constant is ever referenced, so there is nothing
# to link: headers alone satisfy it.
#
# Which is just as well, because DirectXTK CANNOT be built here: its Shaders
# target shells out to CompileShaders.cmd (a Windows .cmd invoking fxc) and dies
# immediately on Linux. SOURCE_SUBDIR points at Inc/, which has no CMakeLists.txt
# — the documented way to populate a dependency WITHOUT add_subdirectory'ing it.
FetchContent_Declare(directxtk
	GIT_REPOSITORY https://github.com/microsoft/DirectXTK
	GIT_TAG may2026
	SOURCE_SUBDIR Inc
	OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(directxtk)
add_library(directxtk_headers INTERFACE)
# BUILD_INTERFACE because the install(TARGETS) below makes this an exportable
# target, and an exported target may not carry build-tree paths. We never
# install, so there is no INSTALL_INTERFACE counterpart to give.
target_include_directories(directxtk_headers SYSTEM INTERFACE
	"$<BUILD_INTERFACE:${directxtk_SOURCE_DIR}/Inc>")
# Same trap as spdlog above: NG's install(EXPORT CommonLibSSE-targets) refuses to
# export a target that links something outside an export set. Nothing is ever
# installed here — this exists purely to satisfy that check.
install(TARGETS directxtk_headers EXPORT CommonLibSSE-targets)
add_library(Microsoft::DirectXTK ALIAS directxtk_headers)

# --- CommonLibSSE-NG: the engine-access library, built as a static lib here.
# SE+AE runtime support (drop VR — not used). Tests off (avoids Catch2).
# alandtse's fork, NOT CharmedBaryon's: that upstream is abandoned at b93280e
# (2024-09-03) and matches the runtime minor version exactly, so it misfiles
# Skyrim 1.7.x as pre-AE. See docs/skse-toolchain.md.
FetchContent_Declare(CommonLibSSE
	GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG
	GIT_TAG v7.1.0)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(CommonLibSSE)

# --- The plugin DLL itself. JSON is the vendored single-header nlohmann/json
# (src/external/nlohmann/json.hpp) — no FetchContent for it: header-only, NG
# never links it, so it can't hit the spdlog export-set problem.
add_library(SkytestProbe SHARED
	src/main.cpp
	src/trace.cpp
	src/resolve.cpp
	src/worldstate.cpp
	src/console_exec.cpp
	src/actor_setup.cpp
	src/dump_actor.cpp
	src/facegen.cpp
	src/facegen_ramp.cpp
	src/mcm.cpp
	src/probes.cpp
	src/hotkey.cpp
	src/commands.cpp)
target_compile_features(SkytestProbe PRIVATE cxx_std_23)
# NOMINMAX is mandatory since NG started including <SimpleMath.h> in RE/S/State.h:
# that drags in the Windows headers, whose min/max MACROS then eat every
# `std::min(`/`std::max(` in this plugin ("expected unqualified-id"). NG defines it
# only PRIVATEly for its own PCH, so each consumer must define it too.
target_compile_definitions(SkytestProbe PRIVATE NOMINMAX)
target_link_libraries(SkytestProbe PRIVATE CommonLibSSE::CommonLibSSE)
target_include_directories(SkytestProbe PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/external")
set_target_properties(SkytestProbe PROPERTIES PREFIX "" SUFFIX ".dll")
