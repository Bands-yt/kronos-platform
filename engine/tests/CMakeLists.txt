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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/plugins/ScriptedPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/plugins/PhysicsPreviewPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/plugins/MovieModePlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/PluginChrome.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/UITheme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/Notification.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/EntityClassification.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/StudioIcons.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/panels/ExplorerPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/panels/InspectorPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/MathExpressionPopup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/CreatorToolsSpawning.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/housedemo/HouseLayout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/CreatorConsole.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/studio/ParticlePresets.cpp
)
target_link_libraries(engine_tests PRIVATE engine_core imgui::imgui)

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
endif()

add_test(NAME engine_tests COMMAND engine_tests)
