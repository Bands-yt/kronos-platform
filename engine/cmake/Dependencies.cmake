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

# --- libcurl (system) -- Kronos ("Google OAuth Authentication"): the real
# OAuth token exchange is an HTTPS POST to Google's own token endpoint
# (oauth2.googleapis.com) -- plain sockets can't do TLS, and hand-rolling
# TLS is a real, serious security liability this codebase has no reason
# to take on. libcurl is the same ubiquitous, well-tested system library
# most native OAuth CLI tools already lean on, same "don't reinvent what
# the host OS/package manager already provides reliably" reasoning as
# SDL2/zlib above. ---------------------------------------------------------
find_package(CURL REQUIRED)

# --- libsecret (system, Linux only) -- Kronos ("Google OAuth
# Authentication" -- "cache it securely"): the real Secret Service D-Bus
# API a real Linux desktop's own keyring daemon (gnome-keyring/kwallet's
# Secret Service shim/etc.) exposes -- see core/CredentialStoreLinux.cpp.
# Windows needs no extra dependency here (DPAPI is a real, built-in
# Win32 API, see core/CredentialStoreWindows.cpp). ---------------------------
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
endif()

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
# Jolt's own GPU-accelerated Hair simulation (Jolt/Physics/Hair/*, compute
# shaders under Jolt/Shaders/*.hlsl) is real but unused by this engine --
# nothing here touches Jolt's hair feature, only its rigid-body/character/
# vehicle physics. These four default to ON in Jolt's own Build/CMakeLists.txt
# and, once any GLSL/HLSL shader compiler is discoverable on PATH, unconditionally
# register a real add_custom_command to compile the Hair shaders via `dxc`
# (DirectXShaderCompiler) -- which this project has no other reason to
# depend on, and which Jolt's own Vulkan/glslc-derived dxc path-lookup
# doesn't gracefully skip when dxc turns out not to exist (see Jolt's own
# TODO comment in Jolt.cmake immediately above that code path). Disabling
# these avoids depending on a compiler this engine doesn't otherwise need.
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
# Jolt's own Build/CMakeLists.txt defaults USE_STATIC_MSVC_RUNTIME_LIBRARY
# to ON on MSVC, which links Jolt.lib against the static CRT (/MT) --
# every other target here (engine_core/engine_runtime/studio/engine_tests)
# uses CMake's own MSVC default, the dynamic CRT (/MD), since nothing in
# this project overrides CMAKE_MSVC_RUNTIME_LIBRARY. Mixing the two
# fails at link time with LNK2038 "mismatch detected for 'RuntimeLibrary'"
# on every real Jolt object file. Forcing this OFF makes Jolt use the
# same dynamic CRT as everything else it links into (studio.exe,
# engine_runtime.exe) -- a no-op on non-MSVC platforms, since Jolt's own
# cmake_dependent_option only applies the static-runtime logic under MSVC.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
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

# --- ImGuiColorTextEdit (Studio Revamp -- "Native Syntax-Highlighting
#     Editor"): the real library the task named, not a hand-rolled
#     approximation -- replaces ImGuiFallbackEditor
#     (studio/panels/ScriptEditorPanel.hpp) as Script Editor's default
#     backend via the existing IScriptEditorBackend seam (see
#     studio/panels/ColorTextEditBackend.hpp). Real native line numbers,
#     syntax highlighting, and error-marker gutter squiggles -- no
#     embedded webview, no Ultralight/CEF dependency, unlike the Monaco
#     path that seam was originally built for. Ships no CMakeLists.txt of
#     its own (two files, TextEditor.h/.cpp) -- same "hand-roll a small
#     static target" treatment as Dear ImGui gets below, for the same
#     reason. No tagged releases exist upstream -- pinned to a specific
#     commit rather than tracking `master`, same reproducible-build
#     reasoning ImGuizmo's own pin below already gives.
FetchContent_Declare(
    imguicolortextedit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG ca2f9f1462e3b60e56351bc466acda448c5ea50d
    GIT_SHALLOW FALSE
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

# --- imnodes (Studio Revamp -- "Node-Based Visual Shader Graph" Phase 2):
#     the real node-graph editor library evaluated against
#     thedmd/imgui-node-editor -- imnodes is the smaller, real tool for
#     this job (BeginNode/BeginInputAttribute/BeginOutputAttribute/link
#     creation, no built-in node "content" beyond what's drawn with
#     plain ImGui calls between them), same "smallest real tool that
#     does the job" call this repo already made for ImGuiColorTextEdit
#     over full Monaco. Same "no CMakeLists.txt of its own worth using"
#     treatment -- imnodes.h/imnodes.cpp/imnodes_internal.h, hand-rolled
#     below. Latest tag (v0.5) is from 2022 and stale against current
#     ImGui; pinned to a specific master commit instead, same
#     reproducible-build reasoning ImGuizmo/ImGuiColorTextEdit's own
#     pins above already give.
# --- tinygltf v3 (Asset Hot-Import Pipeline -- real glTF 2.0
#     loading): evaluated for exactly this reason -- pure C11, zero
#     transitive dependencies (its own arena allocator + a
#     self-contained JSON backend, no STL/exceptions/RTTI, doesn't even
#     need this repo's own already-vendored nlohmann_json/stb_image
#     unless TINYGLTF3_ENABLE_STB_IMAGE is explicitly opted into, which
#     core/GltfLoader.cpp does not -- geometry only, same "mtllib/
#     material assignment parsed-and-ignored" scope cut ObjLoader.hpp's
#     own header comment already states for .obj), fuzz-tested against
#     malicious input. Real tagged release exists (v3.0.1), unlike
#     ImGuiColorTextEdit/imnodes above.
#
# Populate-only, same real reason as imnodes above -- tinygltf's own
# CMakeLists.txt defines no reusable library target at all (only four
# gated-behind-TINYGLTF3_BUILD_TESTS test executables plus a raw-file
# install() rule), so there's nothing useful for FetchContent_MakeAvailable
# to configure here; the real static target is hand-rolled below, same
# treatment as imgui/imguicolortextedit/imnodes.
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v3.0.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tinygltf)
if(NOT tinygltf_POPULATED)
    FetchContent_Populate(tinygltf)
endif()

# --- ufbx (Asset Hot-Import Pipeline -- real FBX loading): Autodesk's
#     own official FBX SDK is proprietary/EULA-gated (a plain fetch of
#     its download page 403s -- the same shape of wall as the
#     Ultralight SDK, see engine/external/ultralight-sdk/README.md);
#     ufbx is the real, standard, license-clean (dual MIT/public-domain)
#     alternative every open-source engine reaches for instead. Two
#     files (ufbx.h/ufbx.c, real C99), no CMakeLists.txt of its own at
#     all (unlike tinygltf/imnodes above, so no populate-only workaround
#     needed here -- there's nothing for FetchContent_MakeAvailable to
#     wrongly auto-configure).
FetchContent_Declare(
    ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG v0.23.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ufbx)

# GIT_SUBMODULES "" -- imnodes' own repo registers a vcpkg submodule
# (used only by its own, unused-here find_package(imgui)-based
# CMakeLists.txt) that a plain recursive clone would otherwise pull down
# for nothing.
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG eb36902c892548ef94f88f51ad7e7c9c7058a71c
    GIT_SHALLOW FALSE
    GIT_SUBMODULES ""
)
# Populate-only, deliberately not via FetchContent_MakeAvailable --
# unlike imgui/imguicolortextedit above (which ship no CMakeLists.txt at
# all, so add_subdirectory-ing them is a real no-op), imnodes ships a
# real one that does find_package(imgui) against a standalone/vcpkg
# imgui install this repo doesn't have (its own imgui target is the
# hand-rolled one below, not a find_package'd one) -- letting
# FetchContent_MakeAvailable auto-add_subdirectory it fails configure.
# Hand-rolled the same way imgui/imguicolortextedit already are, below.
FetchContent_GetProperties(imnodes)
if(NOT imnodes_POPULATED)
    FetchContent_Populate(imnodes)
endif()

FetchContent_MakeAvailable(entt glm volk joltphysics luau enet imgui imguizmo imguicolortextedit nlohmann_json)

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

# ImGuiColorTextEdit ships no CMakeLists.txt of its own either -- same
# treatment as ImGui above, a two-file static target.
add_library(imguicolortextedit STATIC
    ${imguicolortextedit_SOURCE_DIR}/TextEditor.cpp
)
target_include_directories(imguicolortextedit PUBLIC ${imguicolortextedit_SOURCE_DIR})
target_link_libraries(imguicolortextedit PUBLIC imgui::imgui)
add_library(imguicolortextedit::imguicolortextedit ALIAS imguicolortextedit)

# imnodes ships its own CMakeLists.txt but it's built around a different
# (find_package'd) imgui setup than this repo's own hand-rolled `imgui`
# target -- same treatment as imguicolortextedit above.
add_library(imnodes STATIC
    ${imnodes_SOURCE_DIR}/imnodes.cpp
)
target_include_directories(imnodes PUBLIC ${imnodes_SOURCE_DIR})
target_link_libraries(imnodes PUBLIC imgui::imgui)
add_library(imnodes::imnodes ALIAS imnodes)

# tiny_gltf_v3.c is real, plain C11 -- not C++ -- and CMake compiles a
# .c file with the C compiler/standard by default regardless of which
# language the *linking* target is written in, so this Just Works
# alongside engine_core's own C++20 sources without a special-cased
# per-file language override.
add_library(tinygltf STATIC
    ${tinygltf_SOURCE_DIR}/tiny_gltf_v3.c
)
target_include_directories(tinygltf PUBLIC ${tinygltf_SOURCE_DIR})
set_target_properties(tinygltf PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
target_compile_definitions(tinygltf PUBLIC TINYGLTF3_ENABLE_FS) # real filesystem I/O -- resolves a .gltf's sibling .bin buffer files
add_library(tinygltf::tinygltf ALIAS tinygltf)

# ufbx.c is real C99 -- CMake compiles a .c file with the C compiler/
# standard by default regardless of the linking target's own language,
# same as tinygltf above.
add_library(ufbx STATIC
    ${ufbx_SOURCE_DIR}/ufbx.c
)
target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})
set_target_properties(ufbx PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
add_library(ufbx::ufbx ALIAS ufbx)

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
