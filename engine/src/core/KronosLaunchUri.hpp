#pragma once

#include <string>

namespace engine::core {

// Parses the kronos:// launch deep link --
// kronos://launch?game=<slug>[&handoff=<code>] -- registered as a URL
// protocol handler by the installer (see
// installer/src/PlatformIntegration.cpp) and delivered to a running or
// freshly-launched engine_runtime via the --kronos-uri=<uri> argv flag
// (see main.cpp's argv loop).
struct KronosLaunchRequest {
    std::string gameSlug;
    // "Open in Kronos": a real, short-lived, single-use code minted by
    // an authenticated browser session (POST /v1/auth/handoff) and
    // exchanged here for a real session of this process's own (POST
    // /v1/auth/handoff/exchange, see KronosApi::exchangeHandoffCode()).
    //
    // Deliberately a one-time code, not the browser's real access_token
    // riding along in the URI: a custom-scheme URI is handed to the OS's
    // own URL-dispatch machinery, and on both Linux and Windows any
    // other process running as the same user can read another process's
    // full command line (/proc/<pid>/cmdline, Task Manager) -- a
    // long-lived bearer credential in argv is a real local
    // credential-exposure surface, not a hypothetical one.
    //
    // Empty when the URI carried no code at all -- a plain
    // kronos://launch?game=<slug> link (no browser session behind it, or
    // an older client that never minted one) is not malformed; it just
    // means whatever native session (if any) already persisted on this
    // machine is what gets used, exactly as this feature behaved before
    // hand-off existed.
    std::string handoffCode;
};

// Kept free of any Win32/X11/argv concerns on purpose: this is pure
// string parsing, so it can be unit tested without a real OS-level
// protocol registration, which is the part that genuinely cannot be
// exercised in an automated test (there is no way to programmatically
// fire a real OS URL-activation event in a headless CI run).
//
// Returns false -- leaving `outRequest` untouched -- for anything that
// isn't recognizably this exact shape: wrong scheme, wrong action, or
// no game parameter. An unexpected or malformed URI is an honest no-op,
// never a guess at what the caller "probably" meant.
[[nodiscard]] bool parseKronosLaunchUri(const std::string& uri, KronosLaunchRequest& outRequest);

} // namespace engine::core
