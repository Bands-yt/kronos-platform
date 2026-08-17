# ---------------------------------------------------------------------------
# Dependencies.cmake
#
# Every third-party library the core engine needs, in one place, so
# `src/CMakeLists.txt` only ever has to say `target_link_libraries(... EnTT::EnTT ...)`
# and never has to know *how* a dependency got here.
#
# Two acquisition strategies are used, matching docs/ARCHITECTURE.md's stack:
#   - FetchContent for anything we build from source (header-only or small
#     enough to compile alongside us: EnTT, glm, Vulkan-Headers, volk, Jolt,
#     Luau, ENet, Dear ImGui).
#   - find_package() for anything that should come from the host system's
#     package manager (SDL2 -- windowing has real platform integration work
#     that we don't want to reinvent by vendoring it).
#
# Console SDKs (GDK, PS5 SDK, NX SDK) are never declared here. They are
# NDA-gated, platform-holder-distributed, and per docs/ARCHITECTURE.md
# Principle 6 never enter this open repo -- see
# src/platform_adapters/adapters/README.md.
# ---------------------------------------------------------------------------

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

find_package(Threads REQUIRED)

# --- SDL2 (system) ----------------------------------------------------------
find_package(SDL2 CONFIG REQUIRED)

# --- zlib (system) -- Kronos ("Developer Velocity Sprint" -- "One-Click
# Package Exporter" needs a real compressed archive format; deflate is
# the honest, well-understood choice, and a ubiquitous system library,
# same "don't reinvent/vendor what the host OS already provides reliably"
# reasoning as SDL2 above) --------------------------------------------------
find_package(ZLIB REQUIRED)

# --- EnTT (ECS) ---------------------------------------------------------------
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
)

# --- glm (math) ---------------------------------------------------------------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

# --- Vulkan-Headers (pinned to the SDK version matching the system loader) ----
FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG vulkan-sdk-1.4.350.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vulkan_headers)
# Must exist before volk is configured below -- volk's own CMakeLists checks
# for an existing Vulkan::Headers target and skips its fallback find_package().

# --- volk (Vulkan function-pointer loader; dlopen's libvulkan.so.1 at runtime,
#     so we never link against -lvulkan directly) ------------------------------
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.4.350.1
    GIT_SHALLOW TRUE
)

# --- Vulkan Memory Allocator (real GPU buffer/image allocation, not the
#     "no VMA yet" bring-up placeholder from the first pass) ----------------
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vma)
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE ${vma_SOURCE_DIR}/include)
target_link_libraries(vma INTERFACE Vulkan::Headers)
add_library(vma::vma ALIAS vma)

# --- Jolt Physics ---------------------------------------------------------------
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    joltphysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.6.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build
)

# --- Luau (real embedded VM, not a reimplementation) -----------------------
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG 0.732
    GIT_SHALLOW TRUE
)

# --- ENet (reliable-UDP transport for the v1 netcode; QUIC is the v2 upgrade
#     per docs/ARCHITECTURE.md §4.2) -----------------------------------------
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
    GIT_SHALLOW TRUE
)

# --- Dear ImGui (docking branch -- Studio's panel/dock host, §5) -----------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9-docking
    GIT_SHALLOW TRUE
)

# --- nlohmann/json (real JSON, for core::AvatarItemManifest/CatalogueDatabase
#     -- the avatar catalogue system's metadata is explicitly JSON, unlike
#     every other save/load struct in this codebase (Prefab, AnimationClip,
#     PluginManifest, SceneFile, ProjectFile), which use the hand-rolled
#     "KEY value per line" text format instead. That format was always a
#     deliberate choice for small, engine-internal structs with no outside
#     consumer (see PluginManifest.hpp's comment: "no JSON/serialization
#     library ... for a data shape this small"); catalogue metadata is a
#     different case -- it's the one format in this engine explicitly
#     meant to be produced/consumed by tooling outside the engine itself
#     (a real creator-upload pipeline), where JSON interop is the actual
#     requirement, not just a style preference. Header-only; JSON_BuildTests
#     off so FetchContent doesn't also pull in their own test suite.
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

# --- ImGuizmo (the actual library docs/ARCHITECTURE.md §4.2 names --
#     "an immediate-mode overlay (ImGuizmo-style)" -- so this uses the
#     real thing rather than a hand-rolled approximation). Pinned to a
#     specific commit rather than the latest tag (1.83): ImGuizmo's
#     tagged releases lag its ImDrawList-API usage behind current Dear
#     ImGui; HEAD at pin time is what's actually verified to compile
#     against the v1.92.9-docking ImGui pulled in above.
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 5ab7676402ace03cdf930b2d972f59c7d03c6fa8
    GIT_SHALLOW FALSE
)

FetchContent_MakeAvailable(entt glm volk joltphysics luau enet imgui imguizmo nlohmann_json)

# enet's own CMakeLists.txt uses directory-scoped include_directories()
# rather than target_include_directories(), so it never propagates to
# anything linking the `enet` target via FetchContent -- add it back as a
# proper INTERFACE include so `#include <enet/enet.h>` resolves.
target_include_directories(enet INTERFACE ${enet_SOURCE_DIR}/include)

# ImGui ships no CMakeLists.txt of its own -- build the core lib + the SDL2 /
# Vulkan backends we actually use as a small static target.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)
target_link_libraries(imgui PUBLIC SDL2::SDL2 Vulkan::Headers volk)
# Route imgui's Vulkan backend through volk's dynamically-loaded function
# pointers instead of expecting to link -lvulkan directly, matching how
# core::Renderer loads Vulkan (see Renderer.hpp's header comment).
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
add_library(imgui::imgui ALIAS imgui)

# ImGuizmo's own CMakeLists.txt (FetchContent_MakeAvailable already ran
# it, producing the imguizmo::imguizmo target with its `src/` include dir)
# only links against ImGui when built standalone -- as a subdirectory
# here it builds ImGuizmo.cpp without ever pointing it at an imgui.h to
# find, so this link is required, not optional.
target_link_libraries(imguizmo PUBLIC imgui)

# --- miniaudio (vendored single header -- no build step of its own) --------
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_SOURCE_DIR}/external/vendor/miniaudio)
add_library(miniaudio::miniaudio ALIAS miniaudio)

# --- stb_image (vendored single header, public domain -- same "no build
#     step, just an include dir" treatment as miniaudio above). Used by
#     core::Texture for real file-backed texture loading (Material
#     Editor's albedo/metallic/roughness slots, see StudioApp's Material
#     plugin) -- the first thing in this engine that decodes actual image
#     files rather than generating pixels/geometry procedurally.
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_SOURCE_DIR}/external/vendor/stb)
add_library(stb::image ALIAS stb_image)
