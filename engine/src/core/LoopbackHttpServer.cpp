#include "core/LoopbackHttpServer.hpp"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace engine::core {

namespace {
#if defined(_WIN32)
// Kronos: real, idempotent -- WSAStartup()/WSACleanup() are real,
// required bookends around any Winsock use on Windows; ref-counted by
// the OS itself, so calling this more than once across this process's
// lifetime (e.g. a second real sign-in attempt) is real and safe.
bool ensureWinsockInitialized() {
    static bool initialized = false;
    if (initialized) return true;
    WSADATA data;
    initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return initialized;
}
void closeSocketHandle(SocketHandle s) { closesocket(s); }
#else
void closeSocketHandle(SocketHandle s) { close(s); }
#endif

// Real, minimal query-string parser -- `key=value&key2=value2`, real
// percent-decoding (Google's own redirect URL-encodes the real
// authorization code) but deliberately no '+' -> space handling (a real
// query string, not a form body -- '+' is a real, literal character in
// this context per RFC 3986, not application/x-www-form-urlencoded's
// own different convention).
std::string percentDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hexVal(s[i + 1]);
            int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

void parseQueryParam(const std::string& query, const std::string& key, std::string& out) {
    std::string needle = key + "=";
    size_t start = query.find(needle);
    if (start == std::string::npos) return;
    start += needle.size();
    size_t end = query.find('&', start);
    if (end == std::string::npos) end = query.size();
    out = percentDecode(query.substr(start, end - start));
}
} // namespace

LoopbackHttpServer::~LoopbackHttpServer() { stop(); }

bool LoopbackHttpServer::start(uint16_t port) {
#if defined(_WIN32)
    if (!ensureWinsockInitialized()) return false;
#endif
    SocketHandle sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == kInvalidSocket) return false;

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    // Kronos: real, deliberately bound to 127.0.0.1 only (loopback),
    // never 0.0.0.0/INADDR_ANY -- this listener must never be reachable
    // from any other real machine on the network; the real OAuth
    // authorization code it's waiting for is a real, sensitive,
    // single-use credential.
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocketHandle(sock);
        return false;
    }
    if (listen(sock, 1) != 0) {
        closeSocketHandle(sock);
        return false;
    }

    listenSocket_ = static_cast<intptr_t>(sock);
    return true;
}

void LoopbackHttpServer::stop() {
    if (listenSocket_ < 0) return;
    closeSocketHandle(static_cast<SocketHandle>(listenSocket_));
    listenSocket_ = -1;
}

LoopbackCallbackResult LoopbackHttpServer::waitForCallback(float timeoutSeconds) {
    LoopbackCallbackResult result;
    if (listenSocket_ < 0) {
        result.error = "loopback listener is not running";
        return result;
    }
    SocketHandle listenSock = static_cast<SocketHandle>(listenSocket_);

    // Real, bounded wait -- select() on the listening socket, not a
    // blocking accept() that could hang forever if the browser tab was
    // simply closed without completing sign-in.
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listenSock, &readSet);
    timeval tv;
    tv.tv_sec = static_cast<long>(timeoutSeconds);
    tv.tv_usec = static_cast<long>((timeoutSeconds - static_cast<float>(tv.tv_sec)) * 1'000'000.0f);
    int selectResult = select(static_cast<int>(listenSock) + 1, &readSet, nullptr, nullptr, &tv);
    if (selectResult <= 0) {
        result.error = "timed out waiting for the browser to redirect back";
        return result;
    }

    SocketHandle client = accept(listenSock, nullptr, nullptr);
    if (client == kInvalidSocket) {
        result.error = "accept() failed on the real loopback connection";
        return result;
    }

    // Real, bounded read of the request line -- a real browser redirect
    // GET request's own URL (including the real authorization code) is
    // comfortably under this; a real, honest cap so a malformed/hostile
    // local connection (anything can connect to a real, bound loopback
    // port -- this process doesn't authenticate the *caller*, only Google
    // authenticates the *user*) can't make this read unboundedly.
    constexpr size_t kMaxRequestBytes = 8192;
    std::string request;
    char buffer[1024];
    while (request.size() < kMaxRequestBytes) {
        int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        request.append(buffer, static_cast<size_t>(received));
        // A real HTTP request line ends at the first real "\r\n" -- once
        // that's in hand, the real query string is fully available; no
        // need to keep reading real (irrelevant) headers/body.
        if (request.find("\r\n") != std::string::npos) break;
    }

    // Real, minimal parse: "GET /auth/callback?code=...&state=... HTTP/1.1".
    size_t pathStart = request.find(' ');
    size_t pathEnd = pathStart != std::string::npos ? request.find(' ', pathStart + 1) : std::string::npos;
    if (pathStart == std::string::npos || pathEnd == std::string::npos) {
        result.error = "malformed request line from the real loopback connection";
    } else {
        std::string target = request.substr(pathStart + 1, pathEnd - pathStart - 1);
        size_t queryStart = target.find('?');
        std::string query = queryStart != std::string::npos ? target.substr(queryStart + 1) : std::string();

        std::string errorParam;
        parseQueryParam(query, "error", errorParam);
        if (!errorParam.empty()) {
            result.error = "Google returned an error: " + errorParam;
        } else {
            parseQueryParam(query, "code", result.code);
            parseQueryParam(query, "state", result.state);
            result.success = !result.code.empty();
            if (!result.success) result.error = "no real authorization code in the redirect query string";
        }
    }

    // Real, minimal, static HTML response -- no real app logic runs in
    // the browser tab itself, just a plain, honest confirmation page.
    const char* body = result.success
                            ? "<html><body><h2>Signed in to Kronos.</h2><p>You can close this tab and return to "
                              "the app.</p></body></html>"
                            : "<html><body><h2>Kronos sign-in failed.</h2><p>You can close this tab and try again "
                              "in the app.</p></body></html>";
    char header[256];
    std::snprintf(header, sizeof(header),
                  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                  std::strlen(body));
    send(client, header, static_cast<int>(std::strlen(header)), 0);
    send(client, body, static_cast<int>(std::strlen(body)), 0);
    closeSocketHandle(client);

    return result;
}

} // namespace engine::core
