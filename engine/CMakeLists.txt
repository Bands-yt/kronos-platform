cmake_minimum_required(VERSION 3.24)
project(RobloxStyleEngine
    VERSION 0.1.0
    DESCRIPTION "Five Platforms, One Core -- reference engine skeleton"
    LANGUAGES C CXX
)

# Kronos ("Versioning"): the full, real alpha version string --
# project()'s own VERSION field above is numeric-only (CMake can't parse
# a "-alpha" suffix into it), so it stays at the plain 0.1.0 it always
# was; this is the one place a build script or packaging step should
# read the full string from. core/KronosVersion.hpp's kKronosVersion is
# the actual single source of truth every real UI surface (Studio's
# About panel, RuntimeShell's Home/Settings screens, the generated
# project README) reads at runtime -- keep both in sync by hand on each
# real version bump, same as that header's own comment already states.
set(KRONOS_VERSION_STRING "0.1.0-alpha")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Build options
#
# Every optional subsystem defaults ON so a fresh checkout builds the whole
# skeleton described in the architecture doc. Platform holder SDKs (Xbox /
# PlayStation / Switch) are never fetched here -- see
# src/platform_adapters/adapters/README.md -- so those adapters always build
# in their NDA-stub form regardless of these flags.
# ---------------------------------------------------------------------------
option(ENGINE_BUILD_RUNTIME "Build the engine_runtime executable" ON)
option(ENGINE_BUILD_STUDIO  "Build the Studio desktop shell executable" ON)
option(ENGINE_BUILD_TESTS   "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/Dependencies.cmake)

add_subdirectory(src)

# ---------------------------------------------------------------------------
# tests/: assertion-based checks over pure logic (animation interpolation
# math, the IP-infringement scanner, ImportSafetyGuard's tree walk) --
# nothing here needs a GPU, so it's real, fast, CI-shaped verification for
# the parts of engine_core that a live Vulkan run can't meaningfully check
# (a crash-free frame proves the render/physics/audio path didn't blow up;
# it says nothing about whether evaluate() computed the right float).
# ---------------------------------------------------------------------------
if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "RobloxStyleEngine configured:")
message(STATUS "  Build type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build runtime   : ${ENGINE_BUILD_RUNTIME}")
message(STATUS "  Build studio    : ${ENGINE_BUILD_STUDIO}")
message(STATUS "  Build tests     : ${ENGINE_BUILD_TESTS}")
message(STATUS "")
