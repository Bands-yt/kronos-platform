#pragma once

// Test-only ECS component shared by CounterModuleV1.cpp/CounterModuleV2.cpp
// (each built as its own real, separate SHARED library target -- see
// tests/CMakeLists.txt) and test_main.cpp's own
// testCppHotReloadHostSwapsCodeWhileEcsStatePersists(). Deliberately
// trivial: the whole point of this fixture is proving a real dlopen()
// code swap leaves ECS component data (this struct's own `value`)
// completely alone while the *behavior* that mutates it changes from V1
// to V2 -- see core/HotReloadModuleAbi.hpp's own class comment.
struct CounterComponent {
    int value = 0;
};
