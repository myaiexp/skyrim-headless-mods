cmake_minimum_required(VERSION 3.24)
project(AutoCastSpell VERSION 0.1.0 LANGUAGES CXX)

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

# --- rapidcsv: header-only. This mod reads no CSV, but CommonLibSSE-NG's own
# CMake unconditionally uses RAPIDCSV_INCLUDE_DIRS as an include dir (it locates
# rapidcsv via find_path(rapidcsv.h)) — so the var must be set or NG won't
# configure. Pre-seed the cache var it checks; no system lookup needed.
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

# --- The plugin DLL itself.
add_library(AutoCastSpell SHARED src/main.cpp)
target_compile_features(AutoCastSpell PRIVATE cxx_std_23)
target_link_libraries(AutoCastSpell PRIVATE CommonLibSSE::CommonLibSSE)
set_target_properties(AutoCastSpell PROPERTIES PREFIX "" SUFFIX ".dll")
