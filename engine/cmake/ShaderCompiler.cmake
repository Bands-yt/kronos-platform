# ---------------------------------------------------------------------------
# ShaderCompiler.cmake
#
# Kronos ("Studio Revamp" -- "Node-Based Visual Shader Graph", Phase 1):
# real, in-process GLSL -> SPIR-V compilation via Google's shaderc
# (Apache-2.0, fully open-source and FetchContent-fetchable -- unlike the
# Ultralight SDK, this has no licensing gate; see
# engine/external/ultralight-sdk/README.md for that different case).
# This is the load-bearing prerequisite the shader graph needs before any
# node-editor UI is worth building: today shaders only ever compile via
# `glslc` at *build time* (see the top of this directory's own
# Dependencies.cmake-adjacent CMakeLists.txt shader-compile block) --
# nothing in this engine can turn a Studio-authored GLSL string into a
# live VkShaderModule while running. shaderc's libshaderc gives Studio
# that real, in-process compiler; a node graph's own GLSL codegen and a
# Renderer-side runtime-pipeline path are separate, later steps this file
# does not attempt.
#
# shaderc's own CMakeLists.txt (third_party/CMakeLists.txt) expects
# glslang/SPIRV-Tools/SPIRV-Headers to already exist as real
# subdirectories (its own git-sync-deps submodule flow, which this repo
# doesn't use) -- SHADERC_GLSLANG_DIR/SHADERC_SPIRV_TOOLS_DIR/
# SHADERC_SPIRV_HEADERS_DIR below is that same mechanism's own documented
# override, pointed at real FetchContent-populated sources instead.
# Pinned to vulkan-sdk-1.4.350.1 for all three -- the exact same Vulkan
# SDK tag vulkan_headers/volk are already pinned to above, so the whole
# Vulkan-adjacent toolchain in this repo stays one consistent SDK
# version.
# ---------------------------------------------------------------------------

option(KRONOS_BUILD_SHADER_COMPILER "Build shaderc for runtime GLSL->SPIR-V compilation (Shader Graph Phase 1)" ON)

if(NOT KRONOS_BUILD_SHADER_COMPILER)
    message(STATUS "Shader Graph: KRONOS_BUILD_SHADER_COMPILER is OFF -- runtime shader compilation unavailable.")
    return()
endif()

set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
# Kronos: deliberately NOT setting SHADERC_SKIP_INSTALL -- shaderc's own
# third_party/CMakeLists.txt assigns a generator expression
# ($<NOT:${SKIP_GLSLANG_INSTALL}>) to glslang's real GLSLANG_ENABLE_INSTALL
# option via a plain set(), and generator expressions are never evaluated
# in a plain if()/option() context -- the literal string is always
# non-empty/truthy, so glslang always tries to install regardless of this
# flag (a real, confirmed upstream quirk of driving glslang/SPIRV-Tools
# as plain sibling directories instead of shaderc's own git-sync-deps
# submodule flow). Skipping SPIRV-Tools' own install while glslang's
# still runs left SPIRV-Tools-opt out of an export set glslang's
# install(EXPORT) needed it in -- a real CMake Generate-time failure, not
# a warning. Leaving every install() at its own real default keeps both
# sides of that export set consistent; this repo never calls install()
# on these targets itself, so their install rules existing (unused) is
# harmless, not a real cost.
set(SKIP_SPIRV_TOOLS_INSTALL OFF CACHE BOOL "" FORCE) # SPIRV-Tools-opt must actually install/export -- see this file's own comment above on why
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE) # spirv-tools' own CLI tools (spirv-as/spirv-dis/...) -- not needed, just the library
set(SPIRV_HEADERS_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE) # glslangValidator/spirv-remap CLI -- not needed, just the library
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    spirv_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_TAG vulkan-sdk-1.4.350.1
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    spirv_tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_TAG vulkan-sdk-1.4.350.1
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG vulkan-sdk-1.4.350.1
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    shaderc
    GIT_REPOSITORY https://github.com/google/shaderc.git
    GIT_TAG v2026.3
    GIT_SHALLOW TRUE
)

# Populate-only (no add_subdirectory here) -- shaderc/third_party/CMakeLists.txt
# does its own add_subdirectory() on whatever SHADERC_*_DIR points at, so
# calling add_subdirectory() on these ourselves too would register the
# same targets (SPIRV-Tools, glslang, ...) twice and fail to configure.
foreach(_kronos_shaderc_dep spirv_headers spirv_tools glslang shaderc)
    FetchContent_GetProperties(${_kronos_shaderc_dep})
    if(NOT ${_kronos_shaderc_dep}_POPULATED)
        FetchContent_Populate(${_kronos_shaderc_dep})
    endif()
endforeach()

set(SHADERC_SPIRV_HEADERS_DIR "${spirv_headers_SOURCE_DIR}" CACHE STRING "" FORCE)
set(SHADERC_SPIRV_TOOLS_DIR "${spirv_tools_SOURCE_DIR}" CACHE STRING "" FORCE)
set(SHADERC_GLSLANG_DIR "${glslang_SOURCE_DIR}" CACHE STRING "" FORCE)

add_subdirectory(${shaderc_SOURCE_DIR} ${shaderc_BINARY_DIR})

# Real, alias-named consumption target -- same
# imguicolortextedit::imguicolortextedit-style convention every other
# FetchContent dependency in this repo gets, rather than every caller
# spelling out the raw shaderc_combined target name + its include dir
# separately. shaderc_combined already statically links glslang/
# SPIRV-Tools/SPIRV-Tools-opt into one archive (shaderc's own documented
# "the one target embedders should link" convention) -- verified for
# real via a standalone harness (compiled a valid GLSL fragment shader
# to SPIR-V, confirmed the real 0x07230203 magic number; compiled a
# deliberately broken one, confirmed real, accurate diagnostics came
# back) before this target was ever wired into the real build.
add_library(kronos_shaderc INTERFACE)
target_include_directories(kronos_shaderc INTERFACE ${shaderc_SOURCE_DIR}/libshaderc/include)
target_link_libraries(kronos_shaderc INTERFACE shaderc_combined)
# Kronos: same real "KRONOS_WITH_X" compile-time-optionality convention
# UltralightSDK.cmake's own KRONOS_WITH_ULTRALIGHT established -- lets
# RuntimeShaderCompiler.cpp itself compile cleanly (as a real, honest
# "unavailable" stub) whether or not this target is actually linked in,
# rather than needing every add_executable()'s literal source-file list
# (studio's own is one large, flat list already) to conditionally
# include/exclude that one .cpp.
target_compile_definitions(kronos_shaderc INTERFACE KRONOS_WITH_SHADERC=1)
add_library(kronos::shaderc ALIAS kronos_shaderc)

message(STATUS "Shader Graph: shaderc configured (runtime GLSL->SPIR-V compilation available via kronos::shaderc).")
