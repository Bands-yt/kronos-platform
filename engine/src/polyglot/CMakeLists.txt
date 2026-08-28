# Isolated on purpose: polyglot_core depends on nothing but the C++
# standard library + threads. Not linked by engine_core, engine_runtime,
# or studio (see ../../CMakeLists.txt -- this subdirectory is pulled in
# independently, and nothing in src/CMakeLists.txt references
# polyglot_core), so a bug here cannot affect those binaries. See
# README.md in this directory for what is and isn't real yet.

add_library(polyglot_core STATIC
    EventBus.cpp
    UnifiedTypeSystem.cpp
)
target_include_directories(polyglot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(polyglot_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)

add_executable(polyglot_core_tests tests/test_polyglot_core.cpp)
target_link_libraries(polyglot_core_tests PRIVATE polyglot_core Threads::Threads)

add_test(NAME polyglot_core_tests COMMAND polyglot_core_tests)
