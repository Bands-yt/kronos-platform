#include "core/ScriptSecurity.hpp"

#include <lua.h>
#include <lualib.h>

#include "core/ScriptThreadContext.hpp"

namespace engine::core {

const char* securityIdentityName(SecurityIdentity identity) {
    switch (identity) {
        case SecurityIdentity::UserScript: return "UserScript";
        case SecurityIdentity::CoreScript: return "CoreScript";
        case SecurityIdentity::StudioPlugin: return "StudioPlugin";
    }
    // Unreachable for any real enumerator, but an unknown value must
    // still read as the least-privileged thing rather than something
    // that looks trusted.
    return "UserScript";
}

SecurityIdentity currentSecurityIdentity(lua_State* L) {
    if (L == nullptr) return SecurityIdentity::UserScript;
    auto* context = static_cast<ScriptThreadContext*>(lua_getthreaddata(L));
    // Fail closed: a thread with no context attached is not a trusted
    // thread, it is a thread we know nothing about.
    if (context == nullptr) return SecurityIdentity::UserScript;
    return context->identity;
}

void requireSecurityIdentity(lua_State* L, SecurityIdentity minimum, const char* apiName) {
    SecurityIdentity actual = currentSecurityIdentity(L);
    if (securityLevel(actual) >= securityLevel(minimum)) return;

    // luaL_error long-jumps out of this C function -- callers rely on
    // this never returning when the check fails.
    luaL_error(L, "%s is not available to %s (requires %s or higher)", apiName, securityIdentityName(actual),
               securityIdentityName(minimum));
}

} // namespace engine::core
