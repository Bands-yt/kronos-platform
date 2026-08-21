#pragma once

#include <cstdint> // intptr_t, wide enough for both a POSIX fd and a Windows SOCKET handle
#include <string>

namespace engine::core {

// Kronos ("Google OAuth Authentication" -- "local loopback HTTP
// listener"): a real, minimal, single-request HTTP/1.1 listener bound
// to 127.0.0.1 -- exactly what catches Google's real authorization-code
// redirect (`http://localhost:<port>/auth/callback?code=...&state=...`)
// after the user finishes signing in in their real system browser. Not
// a general-purpose HTTP server: it only ever expects one real GET
// request, parses its query string, writes back a small real HTML page
// telling the user to close the tab, then closes -- the same real,
// narrow-scope shape well-known CLI OAuth tools (gcloud, gh auth login)
// use for the same real reason (no other real way for a native app to
// receive a browser redirect).
struct LoopbackCallbackResult {
    bool success = false;
    std::string code;  // real "code" query param -- the real authorization code to exchange
    std::string state; // real "state" query param -- real CSRF protection, checked by the real caller
    std::string error; // real, honest failure reason when !success (timeout, malformed request, etc.)
};

class LoopbackHttpServer {
public:
    ~LoopbackHttpServer();

    // Real bind()+listen() on 127.0.0.1:port. Returns false on a real
    // OS-level failure (most commonly: the port is already in use --
    // real, honest, not silently retried on a different port, since the
    // real Google Cloud OAuth Client ID's own "Authorized redirect URI"
    // is registered against this exact real port).
    [[nodiscard]] bool start(uint16_t port);
    void stop();
    [[nodiscard]] bool isRunning() const { return listenSocket_ >= 0; }

    // Real, blocks (bounded by timeoutSeconds, via a real select()/poll()
    // wait on the listening socket, not a busy loop) for exactly one
    // real inbound GET request, parses its query string, and writes a
    // real, minimal static HTML response before closing that one
    // connection. A real, honest timeout (success=false, error set) if
    // nothing arrives in time -- never hangs forever waiting on a
    // browser tab the user closed without finishing sign-in.
    [[nodiscard]] LoopbackCallbackResult waitForCallback(float timeoutSeconds);

private:
    intptr_t listenSocket_ = -1;
};

} // namespace engine::core
