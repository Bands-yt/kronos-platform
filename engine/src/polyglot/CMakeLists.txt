# Kronos (Beta Roadmap "Package Registry Integration" / "VFS Mounting"):
# polyglot_core still depends on nothing but the C++ standard library +
# threads, but is now linked into engine_core (see ../CMakeLists.txt's
# own target_link_libraries) so PackageRegistry/VirtualFileSystem are
# real, usable engine functionality, not an isolated experiment. Both
# EventBus.cpp/UnifiedTypeSystem.cpp and PackageRegistry.cpp/
# VirtualFileSystem.cpp have been checked clean under ASan+UBSan (see
# README.md) before this link was added. See README.md in this directory
# for what is and isn't real yet -- the Luau/Rust/WASM cross-language
# pieces (CrossLanguageDebugger.hpp, HotReload.hpp) still are not: no
# such runtimes exist in this codebase to bridge to.

add_library(polyglot_core STATIC
    EventBus.cpp
    UnifiedTypeSystem.cpp
    PackageRegistry.cpp
    VirtualFileSystem.cpp
)
target_include_directories(polyglot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(polyglot_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)

add_executable(polyglot_core_tests tests/test_polyglot_core.cpp)
target_link_libraries(polyglot_core_tests PRIVATE polyglot_core Threads::Threads)

add_test(NAME polyglot_core_tests COMMAND polyglot_core_tests)
