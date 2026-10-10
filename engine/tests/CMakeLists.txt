# No test framework dependency (no GoogleTest/Catch2 fetched) -- the
# assertion-based harness in test_main.cpp is plenty for this test count
# and keeps the first real test target dependency-free. If this grows
# past a few dozen cases, swapping in a real framework is a contained
# change to this one file, not a rewrite of the tests themselves.
#
# studio/PluginManifest.cpp and studio/plugins/ScriptedPlugin.cpp are
# compiled here too (in addition to the `studio` executable target in
# src/CMakeLists.txt -- a small .cpp compiled into two independent
# executables is harmless, there's no ODR/linkage conflict across
# separate binaries) so plugin-manifest and plugin-loader logic gets the
# same real, no-mocks test coverage as everything else in this file
# instead of being untestable dead weight. ScriptedPlugin.cpp's
# drawPanel() calls into ImGui (even though no test here calls
# drawPanel() itself) -- its .o still needs every symbol it references
# resolved at link time, hence imgui::imgui below; this is the one
# exception to "dependency-free" above, pulled in by that single method.
# PhysicsPreviewPlugin.cpp is here for the same reason: its play()/stop()/
# update()/castTestRay() are real, pure ECS+Physics logic worth testing
# headlessly (same as everything in core/Physics.cpp -- see this file's
# real, live, headless Jolt tests below), and only drawPanel() -- never
# called here -- touches ImGui.
#
# Sprint 7 ("Studio UI Revamp") adds five more, same reasoning: Notification.cpp
# (NotificationCenter::push()/tick() are pure queue logic; draw() is ImGui-only
# and never called here), EntityClassification.cpp (classifyEntity() is
# ECS-only, no ImGui at all), StudioIcons.cpp + ExplorerPanel.cpp (linked
# together -- ExplorerPanel::animateOpenAmount() is the real, pure animation-
# convergence math extracted specifically so it's testable; draw() itself,
# which references StudioIcons' drawIcon(), is never called here but still
# needs every symbol it touches resolved at link time, same as
# ScriptedPlugin.cpp above), and CreatorToolsSpawning.cpp (the ECS-only half
# of CreatorToolsPlugin's authoring actions, split out specifically so
# entity-creation logic doesn't stay locked behind a live Vulkan device --
# see that header's own comment).
add_executable(engine_tests
    test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/PluginManifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/LocalPluginDirectory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/StudioEcsScriptApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ScriptCinematicApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ScriptRenderApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ScriptPhysicsPreviewApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/plugins/ScriptedPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/plugins/PhysicsPreviewPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/plugins/AudioMixerPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/plugins/MovieModePlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/plugins/ModelingModePlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/FileBrowse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/PluginChrome.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/UITheme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/UIWidgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RobloxLeaderboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RobloxGui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/Notification.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/EntityClassification.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/StudioIcons.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/EntityOps.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/panels/ExplorerPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/panels/InspectorPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/MathExpressionPopup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/CreatorToolsSpawning.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/housedemo/HouseLayout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/CreatorConsole.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ParticlePresets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/RuntimeShaderCompiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ShaderGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ShaderGraphCodegen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/SurfaceGraphMaterials.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/VisualScript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/VisualScriptCompiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ParticleComputeGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ParticleComputeCodegen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/panels/ColorTextEditBackendText.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/panels/LuauSymbolIndex.cpp
)
target_link_libraries(engine_tests PRIVATE engine_core imgui::imgui)
if(TARGET kronos::shaderc)
    target_link_libraries(engine_tests PRIVATE kronos::shaderc)
endif()

if(WIN32)
    # SDL2's SDL_main.h (reached transitively through engine_core's own
    # headers) #defines main to SDL_main on Windows and expects the
    # SDL2main library to supply the real entry point. engine_runtime and
    # studio are real GUI apps that do call SDL_Init(SDL_INIT_VIDEO), so
    # they link SDL2main; this test runner is a real console binary that
    # never opens a window, so the correct fix here is SDL's own
    # documented "I provide my own main()" opt-out rather than pulling in
    # a GUI entry point it has no use for.
    target_compile_definitions(engine_tests PRIVATE SDL_MAIN_HANDLED)

    # Kronos ("Windows Release CI" -- real build failure): test_main.cpp
    # is one, deliberately single-file assertion suite (14000+ real
    # checks, see this file's own header comment on why no test framework
    # split it up) -- real, live-confirmed MSVC error C1128 ("number of
    # sections exceeded object file format limit") compiling it, the
    # standard COFF object-format ceiling a single translation unit this
    # large runs into on MSVC specifically (GCC/Clang have no equivalent
    # limit, which is why this was never hit building on Linux). /bigobj
    # is MSVC's own documented, real fix -- widens the section-count field
    # in the .obj it emits for this one target, not a workaround.
    target_compile_options(engine_tests PRIVATE /bigobj)
endif()

add_test(NAME engine_tests COMMAND engine_tests)

# Kronos ("Logic Hot-Reloading" -- v0.4.0 Creator Suite): two real,
# separate SHARED library targets implementing the exact same
# CounterComponent-mutating IHotReloadableModule with two different real
# behaviors (see hotreload_fixtures/CounterComponent.hpp's own comment).
# core::CppHotReloadHost's own tests dlopen() these by their real built
# path to prove an actual runtime code swap, not a simulated one.
#
# Deliberately linked against EnTT::EnTT/glm::glm directly (both real
# header-only interface targets, see src/CMakeLists.txt's own
# target_link_libraries(engine_core ...) list) rather than engine_core
# itself: engine_core is a real static library built WITHOUT -fPIC (the
# correct default for something only ever linked into the two real
# executables), so linking its own .a into a SHARED target fails at
# link time ("relocation ... can not be used when making a shared
# object"). ECS.hpp/Components.hpp are themselves header-only against
# entt+glm, so no compiled engine_core code is actually needed to
# implement IHotReloadableModule here -- these two module .cpp files
# get their own fresh, real -fPIC objects (CMake's own default for a
# direct SHARED-target source), same effective ABI (same compiler, same
# C++ standard, same entt/glm versions) as CppHotReloadHost.hpp's own
# class comment requires, just without engine_core's non-PIC object
# code along for the ride.
add_library(hotreload_counter_module_v1 SHARED hotreload_fixtures/CounterModuleV1.cpp)
target_include_directories(hotreload_counter_module_v1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(hotreload_counter_module_v1 PRIVATE EnTT::EnTT glm::glm)
target_compile_features(hotreload_counter_module_v1 PRIVATE cxx_std_20)

add_library(hotreload_counter_module_v2 SHARED hotreload_fixtures/CounterModuleV2.cpp)
target_include_directories(hotreload_counter_module_v2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(hotreload_counter_module_v2 PRIVATE EnTT::EnTT glm::glm)
target_compile_features(hotreload_counter_module_v2 PRIVATE cxx_std_20)

# Real, absolute build-tree paths baked in as a #define -- same
# convention src/CMakeLists.txt's own ENGINE_SHADER_DIR/ENGINE_ASSET_DIR
# already establish for "a test needs to find a real build output/asset
# by absolute path, not a guess at the working directory it happens to
# be run from."
foreach(stateful_module A B C)
    add_library(hotreload_stateful_${stateful_module} SHARED hotreload_fixtures/StatefulModule${stateful_module}.cpp)
    target_include_directories(hotreload_stateful_${stateful_module} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(hotreload_stateful_${stateful_module} PRIVATE EnTT::EnTT glm::glm)
    target_compile_features(hotreload_stateful_${stateful_module} PRIVATE cxx_std_20)
    add_dependencies(engine_tests hotreload_stateful_${stateful_module})
endforeach()

add_dependencies(engine_tests hotreload_counter_module_v1 hotreload_counter_module_v2)

# C plugins for the stable plugin API (plugin/kronos_plugin.h), built as C.
set(_kronos_test_plugins
    "test_plugin|"
    "test_plugin_v2|PLUGIN_ID=\"test.plugin.v2\",IMPORTER_VERSION=2,OUTPUT_PREFIX=\"V2:\""
    "test_plugin_dup|PLUGIN_ID=\"test.plugin.dup\""
    "test_plugin_limited|PLUGIN_ID=\"test.plugin.limited\",PLUGIN_CAPS=KRONOS_CAP_EDITOR_PANELS|KRONOS_CAP_SCENE_READ"
    "test_plugin_future|PLUGIN_ID=\"test.plugin.future\",PLUGIN_API_MAJOR=2"
)
foreach(_entry IN LISTS _kronos_test_plugins)
    string(FIND "${_entry}" "|" _bar)
    string(SUBSTRING "${_entry}" 0 ${_bar} _name)
    math(EXPR _rest "${_bar} + 1")
    string(SUBSTRING "${_entry}" ${_rest} -1 _defs)
    string(REPLACE "," ";" _defs "${_defs}")
    add_library(${_name} SHARED plugin_fixtures/TestPlugin.c)
    target_include_directories(${_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    set_target_properties(${_name} PROPERTIES C_STANDARD 99 C_VISIBILITY_PRESET hidden)
    if(_defs)
        target_compile_definitions(${_name} PRIVATE ${_defs})
    endif()
    add_dependencies(engine_tests ${_name})
    string(TOUPPER "${_name}" _upper)
    target_compile_definitions(engine_tests PRIVATE ${_upper}_PATH="$<TARGET_FILE:${_name}>")
endforeach()
if(TARGET sample_c_plugin)
    add_dependencies(engine_tests sample_c_plugin)
    target_compile_definitions(engine_tests PRIVATE SAMPLE_C_PLUGIN_PATH="$<TARGET_FILE:sample_c_plugin>")
endif()
if(TARGET kronos_plugin_sandbox)
    add_dependencies(engine_tests kronos_plugin_sandbox)
    target_compile_definitions(engine_tests PRIVATE KRONOS_PLUGIN_SANDBOX_PATH="$<TARGET_FILE:kronos_plugin_sandbox>")
endif()

target_compile_definitions(engine_tests PRIVATE
    HOTRELOAD_MODULE_V1_PATH="$<TARGET_FILE:hotreload_counter_module_v1>"
    HOTRELOAD_MODULE_V2_PATH="$<TARGET_FILE:hotreload_counter_module_v2>"
    HOTRELOAD_STATEFUL_A_PATH="$<TARGET_FILE:hotreload_stateful_A>"
    HOTRELOAD_STATEFUL_B_PATH="$<TARGET_FILE:hotreload_stateful_B>"
    HOTRELOAD_STATEFUL_C_PATH="$<TARGET_FILE:hotreload_stateful_C>"
)
# 2-frame lossless fixture (red @ 0s, blue @ 1s, 64x64); only meaningful
# when the decoder is built with FFmpeg.
if(TARGET PkgConfig::LIBAV)
    target_compile_definitions(engine_tests PRIVATE
        VIDEO_DECODER_TEST_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video_decoder_test.mp4")
endif()
