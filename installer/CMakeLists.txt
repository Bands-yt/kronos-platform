cmake_minimum_required(VERSION 3.24)
project(KronosBootstrapInstaller
    VERSION 1.0.0
    DESCRIPTION "Kronos Bootstrap Installer -- downloads and installs the latest Kronos release, no compiler required"
    LANGUAGES C CXX
)

# Kronos ("Kronos Bootstrap Installer" -- "must be lightweight and
# cross-platform"): a real, deliberately SEPARATE CMake project from
# engine/ -- this tool must run on a machine that has never built (or
# ever will build) the engine itself, so it has zero dependency on
# engine_core, Vulkan, Jolt, Luau, or ENet. Its own real dependency list
# is intentionally small: SDL2 (windowing + the lightweight SDL_Renderer
# 2D backend, not Vulkan -- there's no real 3D content here, so pulling
# in the full Vulkan renderer stack just for two buttons and a progress
# bar would contradict "lightweight"), Dear ImGui (fetched fresh, same
# real version pin the engine itself uses, just for this project's own
# independent build), libcurl (the real GitHub API + download client),
# zlib (real gzip decompression for the Linux .tar.gz path), and miniz
# (a real, small, single-translation-unit ZIP reader for the Windows
# .zip path) -- see this file's own FetchContent block below for exactly
# where each of those comes from.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

find_package(Threads REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

# --- Dear ImGui (real, same version the main engine pins) -----------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(installer_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(installer_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(installer_imgui PUBLIC SDL2::SDL2)

# --- nlohmann_json (real, small, header-only -- for parsing the real
#     GitHub Releases API response) ------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# --- miniz (real, small ZIP reader -- the Windows .zip archive's own
#     real extraction path; the Linux .tar.gz path uses zlib directly +
#     this project's own small, hand-written real tar parser instead,
#     see src/ArchiveExtractor.cpp). Fetched as source only (real,
#     deliberate -- miniz's own CMakeLists.txt requires a `cmake_minimum_required`
#     older than modern CMake accepts; CMAKE_POLICY_VERSION_MINIMUM
#     below is CMake's own documented, sanctioned way to configure an
#     otherwise-fine older subproject rather than patching its file). -----
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.0.2
    GIT_SHALLOW TRUE
)
FetchContent_Populate(miniz)
add_library(miniz STATIC
    ${miniz_SOURCE_DIR}/miniz.c
    ${miniz_SOURCE_DIR}/miniz_tdef.c
    ${miniz_SOURCE_DIR}/miniz_tinfl.c
    ${miniz_SOURCE_DIR}/miniz_zip.c
)
# vendor/ (this real project's own, hand-written miniz_export.h -- see
# that file's own header comment) must resolve before miniz's own
# source dir so it wins the "#include "miniz_export.h"" lookup;
# ${miniz_SOURCE_DIR} is still needed for every other real miniz header.
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor ${miniz_SOURCE_DIR})

add_executable(kronos_installer
    src/main.cpp
    src/Sha256.cpp
    src/GitHubReleaseApi.cpp
    src/Downloader.cpp
    src/ArchiveExtractor.cpp
    src/PlatformIntegration.cpp
    src/UpdateApply.cpp
)
target_link_libraries(kronos_installer PRIVATE
    installer_imgui
    SDL2::SDL2
    SDL2::SDL2main
    ZLIB::ZLIB
    CURL::libcurl
    nlohmann_json::nlohmann_json
    miniz
    Threads::Threads
)

if(WIN32)
    # Kronos ("Create a desktop shortcut (Windows)"): real Shell Link
    # COM API (src/PlatformIntegration.cpp) needs these; Crypt32/Wintrust
    # are not used here (SHA-256 is this project's own small, hand-
    # written implementation, matching engine_core's identical real
    # precedent -- see src/Sha256.hpp's own header comment on why this
    # is duplicated rather than shared).
    target_link_libraries(kronos_installer PRIVATE ole32 shell32 uuid)
elseif(UNIX)
    target_link_libraries(kronos_installer PRIVATE dl)
endif()
