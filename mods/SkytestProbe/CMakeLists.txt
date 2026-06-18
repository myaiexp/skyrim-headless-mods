cmake_minimum_required(VERSION 3.24)
project(SkytestProbe VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

# --- spdlog: satisfies CommonLibSSE-NG's find_package(spdlog CONFIG REQUIRED).
# OVERRIDE_FIND_PACKAGE redirects that find_package() to this built target.
# Pinned to the fmt-10 era (v1.13.0) to match what NG was developed against;
# newer spdlog bundles fmt 11 whose stricter API breaks NG's formatting.
FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog
	GIT_TAG v1.13.0
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

# --- CommonLibSSE-NG: the engine-access library, built as a static lib here.
# SE+AE runtime support (drop VR — not used). Tests off (avoids Catch2).
FetchContent_Declare(CommonLibSSE
	GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG
	GIT_TAG b93280e832f263dbef44e44cbe2936622a02f91a)
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
target_link_libraries(SkytestProbe PRIVATE CommonLibSSE::CommonLibSSE)
target_include_directories(SkytestProbe PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/external")
set_target_properties(SkytestProbe PROPERTIES PREFIX "" SUFFIX ".dll")
