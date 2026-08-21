#pragma once

#include <chrono>

#include "core/ScriptSecurity.hpp"

namespace engine::core {

// Kronos ("Luau Sandbox & Security Manager"): the real per-VM state
// hung off every Luau thread via lua_setthreaddata(), shared by a
// script's owning lua_State and every coroutine spawned from it (see
// Scripting::spawnThread, which copies the pointer).
//
// Internal to the scripting implementation -- deliberately not part of
// Scripting.hpp's public surface. It is reachable only from C++; no Lua
// value ever points at it, which is what makes `identity` below
// unforgeable from inside a script.
struct ScriptThreadContext {
    // The watchdog deadline every thread on this VM shares. Reset
    // immediately before each lua_resume (see Scripting::refreshDeadline).
    std::chrono::steady_clock::time_point deadline;

    // The privilege level this whole VM runs at. Fixed at VM creation
    // and never mutated afterwards -- there is intentionally no API,
    // C++ or Lua, to raise it on a live VM, so there is no
    // "escalate then act" window to attack.
    SecurityIdentity identity = SecurityIdentity::UserScript;
};

} // namespace engine::core
