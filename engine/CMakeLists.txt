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

# ---------------------------------------------------------------------------
# CPack: a real installer/zip built from src/CMakeLists.txt's own
# install() rules (see that file's own comment for what those actually
# bundle and why). This section is genuinely new -- there was no CPack
# config anywhere in this tree before this pass, which is the concrete
# gap underneath "kronos-windows-x64 has no real, repeatable packaging
# story" (scripts/package_alpha.sh, the one packaging script that did
# exist, is real but Linux/bash-only, see its own header comment).
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "KronosStudio")
set(CPACK_PACKAGE_VENDOR "Kronos")
set(CPACK_PACKAGE_VERSION "${KRONOS_VERSION_STRING}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Kronos Studio")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
# Real, flat, relocatable install layout (src/CMakeLists.txt's own
# install(DESTINATION .) for the binaries) -- matches
# core::resolveResourceDir()'s own expectations; a versioned subfolder
# inside the package would break it, so this is deliberately off.
set(CPACK_PACKAGE_RELOCATABLE ON)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

if(WIN32)
    # NSIS for a real installer (Start Menu shortcuts, uninstaller); ZIP
    # alongside it as the no-install-required option Jay's own "just run
    # the exe" workflow needs.
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "Kronos Studio")
    set(CPACK_NSIS_PACKAGE_NAME "Kronos Studio ${KRONOS_VERSION_STRING}")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    if(ENGINE_BUILD_STUDIO)
        set(CPACK_NSIS_INSTALLED_ICON_NAME "studio.exe")
        set(CPACK_PACKAGE_EXECUTABLES "studio;Kronos Studio")
    endif()
    if(ENGINE_BUILD_RUNTIME)
        list(APPEND CPACK_PACKAGE_EXECUTABLES "engine_runtime;Kronos")
    endif()
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
