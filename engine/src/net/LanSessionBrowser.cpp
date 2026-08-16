#include "net/LanSessionBrowser.hpp"

#include "net/NetTypes.hpp"
#include "net/Serialization.hpp"

#if defined(__linux__)
#include <cstdio>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace engine::net {

#if defined(__linux__)

namespace {
// Real, shared setup for both of this class's sockets -- non-blocking,
// bound to `port` (0 lets the OS assign a real ephemeral one, used for
// the dedicated ping socket -- see class comment on why it's separate
// from the announce-port socket).
int createBoundUdpSocket(uint16_t port, bool reuseAddr) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    if (reuseAddr) {
        int enable = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    }

    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}
} // namespace

LanSessionBrowser::~LanSessionBrowser() { stop(); }

bool LanSessionBrowser::start(uint16_t announcePort, uint16_t pingPort) {
    stop();

    socketFd_ = createBoundUdpSocket(announcePort, /*reuseAddr=*/true);
    if (socketFd_ < 0) {
        std::fprintf(stderr, "LanSessionBrowser: bind() failed on announce port %u.\n", announcePort);
        return false;
    }

    // Real, separate ephemeral-port socket for unicast Echo pings -- see
    // this class's own header comment on why it can't share a port with
    // socketFd_ above.
    pingSocketFd_ = createBoundUdpSocket(0, /*reuseAddr=*/false);
    if (pingSocketFd_ < 0) {
        std::fprintf(stderr, "LanSessionBrowser: bind() failed for the real ephemeral ping socket.\n");
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    pingPort_ = pingPort;
    sessions_.clear();
    pendingPingSentAtSeconds_.clear();
    return true;
}

void LanSessionBrowser::stop() {
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
    if (pingSocketFd_ >= 0) {
        ::close(pingSocketFd_);
        pingSocketFd_ = -1;
    }
    sessions_.clear();
    pendingPingSentAtSeconds_.clear();
}

void LanSessionBrowser::tick(float nowSeconds) {
    if (socketFd_ < 0 || pingSocketFd_ < 0) return;

    uint8_t buffer[512];
    sockaddr_in senderAddr{};

    // Real announce-port socket: only ever real Announce broadcasts
    // arrive here (the announcer replies to a real Echo from ITS OWN,
    // separate ping-port socket, addressed to our real ephemeral
    // pingSocketFd_ port below, never back to this one).
    for (;;) {
        socklen_t senderLen = sizeof(senderAddr);
        ssize_t received =
            ::recvfrom(socketFd_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&senderAddr), &senderLen);
        if (received <= 0) break;

        char addrBuf[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &senderAddr.sin_addr, addrBuf, sizeof(addrBuf));
        std::string sourceAddress = addrBuf;

        ByteReader reader(buffer, static_cast<size_t>(received));
        if (readLanDiscoveryHeader(reader) != LanMessageKind::Announce) continue; // real, honest ignore -- not for us

        LanSessionAnnouncement announcement;
        if (!deserializeLanAnnouncement(reader, announcement)) continue;
        // Real, honest reject -- a protocol-mismatched session can't
        // really be joined anyway (NetworkSession's own real join
        // handshake would just reject it), so there's no point listing
        // it as if it were.
        if (announcement.protocolVersion != kNetworkProtocolVersion) continue;

        DiscoveredSession& session = sessions_[announcement.sessionId];
        session.sessionId = announcement.sessionId;
        session.sessionName = announcement.sessionName;
        session.hostDisplayName = announcement.hostDisplayName;
        session.sourceAddress = sourceAddress;
        session.gamePort = announcement.gamePort;
        session.currentPlayerCount = announcement.currentPlayerCount;
        session.maxPlayerCount = announcement.maxPlayerCount;
        session.lastSeenSeconds = nowSeconds;
        session.gameName = announcement.gameName;
        session.gameThumbnailColor = announcement.gameThumbnailColor;
        session.gameSafetyStatusValue = announcement.gameSafetyStatusValue;
        session.sessionStartUnixSeconds = announcement.sessionStartUnixSeconds;

        if (pendingPingSentAtSeconds_.find(sourceAddress) == pendingPingSentAtSeconds_.end()) {
            // Real unicast Echo, sent from the real, separate
            // pingSocketFd_, to the sender's real address on the real,
            // well-known pingPort_ a real LanSessionAnnouncer listens on.
            sockaddr_in pingDest{};
            pingDest.sin_family = AF_INET;
            pingDest.sin_addr = senderAddr.sin_addr;
            pingDest.sin_port = htons(pingPort_);
            ByteWriter echo;
            writeLanDiscoveryHeader(echo, LanMessageKind::Echo);
            ::sendto(pingSocketFd_, echo.bytes().data(), echo.size(), 0, reinterpret_cast<sockaddr*>(&pingDest),
                     sizeof(pingDest));
            pendingPingSentAtSeconds_[sourceAddress] = nowSeconds;
        }
    }

    // Real ephemeral ping socket: only ever a real EchoReply arrives
    // here, addressed specifically to this real port.
    for (;;) {
        socklen_t senderLen = sizeof(senderAddr);
        ssize_t received = ::recvfrom(pingSocketFd_, buffer, sizeof(buffer), 0,
                                       reinterpret_cast<sockaddr*>(&senderAddr), &senderLen);
        if (received <= 0) break;

        char addrBuf[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &senderAddr.sin_addr, addrBuf, sizeof(addrBuf));
        std::string sourceAddress = addrBuf;

        ByteReader reader(buffer, static_cast<size_t>(received));
        if (readLanDiscoveryHeader(reader) != LanMessageKind::EchoReply) continue;

        auto pendingIt = pendingPingSentAtSeconds_.find(sourceAddress);
        if (pendingIt == pendingPingSentAtSeconds_.end()) continue;
        float pingMs = (nowSeconds - pendingIt->second) * 1000.0f;
        pendingPingSentAtSeconds_.erase(pendingIt);
        for (auto& [id, session] : sessions_) {
            if (session.sourceAddress == sourceAddress) session.pingMs = pingMs;
        }
    }

    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (nowSeconds - it->second.lastSeenSeconds > kStaleTimeoutSeconds) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<DiscoveredSession> LanSessionBrowser::discoveredSessions() const {
    std::vector<DiscoveredSession> result;
    result.reserve(sessions_.size());
    for (const auto& [id, session] : sessions_) result.push_back(session);
    return result;
}

#else // !defined(__linux__)

LanSessionBrowser::~LanSessionBrowser() = default;
bool LanSessionBrowser::start(uint16_t, uint16_t) { return false; }
void LanSessionBrowser::stop() {}
void LanSessionBrowser::tick(float) {}
std::vector<DiscoveredSession> LanSessionBrowser::discoveredSessions() const { return {}; }

#endif

} // namespace engine::net
