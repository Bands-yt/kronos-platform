#pragma once

#include <string>

struct lua_State;

namespace engine::core {

// Kronos ("Luau Sandbox & Security Manager" -- "Execution Contexts"):
// the real privilege level a given script runs at. The numeric values
// are deliberately sparse and match the levels the design asked for, so
// there is room to add intermediate identities later without renumbering
// (and without silently changing the meaning of a persisted number).
//
// The ordering is the security contract: a capability available at level
// N is available at every level >= N, and never below it.
enum class SecurityIdentity : int {
    // User-generated content. The default for anything uploaded, and the
    // level every unknown/untrusted script must be given. Assume the
    // author of a Level 0 script is actively hostile.
    UserScript = 0,
    // Scripts shipped as part of a real Kronos game/experience by the
    // engine itself -- trusted to drive gameplay, still not trusted to
    // touch editor internals.
    CoreScript = 4,
    // Studio plugins, which legitimately need editor-level APIs a game
    // script must never reach.
    StudioPlugin = 6,
};

[[nodiscard]] inline int securityLevel(SecurityIdentity identity) { return static_cast<int>(identity); }

[[nodiscard]] const char* securityIdentityName(SecurityIdentity identity);

// The real identity the thread `L` is running under. Reads it from the
// per-VM state Scripting attaches at creation, so it is a property of the
// running VM rather than anything a script can reach or forge -- there is
// deliberately no Lua-visible way to set, raise, or even read this.
//
// Coroutines inherit their creator's identity (Scripting::spawnThread
// copies the thread data), which is what stops a Level 0 script from
// escalating simply by doing its work inside coroutine.create().
//
// Returns UserScript (the LEAST privileged level) if the identity cannot
// be determined for any reason. That default is deliberate: an unknown
// identity must fail closed, never open.
[[nodiscard]] SecurityIdentity currentSecurityIdentity(lua_State* L);

// Guard for a C binding that must only be callable at or above
// `minimum`. Raises a real Luau error (long-jumps, never returns) when
// the caller is not privileged enough, so a binding can call this as its
// first statement and then proceed unconditionally.
//
// This is defense-in-depth, NOT the primary mechanism. The primary
// mechanism is that an elevated API is never registered into a
// lower-privileged VM's global table at all, so a Level 0 script has no
// reference to reach in the first place -- see Scripting::registerBindings().
// This guard exists for the cases where one C function is genuinely
// shared across identities and has to branch on privilege internally.
void requireSecurityIdentity(lua_State* L, SecurityIdentity minimum, const char* apiName);

} // namespace engine::core
