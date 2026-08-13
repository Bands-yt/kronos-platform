#include "net/LanSessionAnnouncer.hpp"

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

LanSessionAnnouncer::~LanSessionAnnouncer() { stop(); }

bool LanSessionAnnouncer::start(const std::string& destinationAddress, uint16_t announcePort, uint16_t pingPort) {
    stop(); // real, honest idempotency -- a second start() replaces any real prior socket rather than leaking it

    socketFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        std::fprintf(stderr, "LanSessionAnnouncer: socket() failed.\n");
        return false;
    }

    int enable = 1;
    ::setsockopt(socketFd_, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));
    ::setsockopt(socketFd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    int flags = ::fcntl(socketFd_, F_GETFL, 0);
    ::fcntl(socketFd_, F_SETFL, flags | O_NONBLOCK);

    // Bound to pingPort (NOT announcePort) so this same socket can
    // receive+reply to real inbound Echo pings without ever sharing a
    // port with a real, possibly co-located LanSessionBrowser's own
    // announcePort-bound receive socket -- see LanDiscoveryProtocol.hpp's
    // own kLanAnnouncePort/kLanPingPort comment for the real bug this
    // split fixes.
    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = htons(pingPort);
    if (::bind(socketFd_, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0) {
        std::fprintf(stderr, "LanSessionAnnouncer: bind() failed on ping port %u.\n", pingPort);
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    destinationAddress_ = destinationAddress;
    announcePort_ = announcePort;
    // Real, immediate first announce -- a freshly-hosted session
    // shouldn't make a real browser wait a full kAnnounceIntervalSeconds
    // just to see it appear.
    announceTimer_ = kAnnounceIntervalSeconds;
    return true;
}

void LanSessionAnnouncer::stop() {
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
    announceTimer_ = 0.0f;
}

void LanSessionAnnouncer::tick(float dt, const LanSessionAnnouncement& announcement) {
    if (socketFd_ < 0) return;

    uint8_t buffer[512];
    sockaddr_in senderAddr{};
    for (;;) {
        socklen_t senderLen = sizeof(senderAddr);
        ssize_t received =
            ::recvfrom(socketFd_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&senderAddr), &senderLen);
        if (received <= 0) break; // real, honest end-of-queue (EWOULDBLOCK/EAGAIN) or a genuine read error either way

        ByteReader reader(buffer, static_cast<size_t>(received));
        if (readLanDiscoveryHeader(reader) != LanMessageKind::Echo) continue; // real, honest ignore -- not for us

        ByteWriter reply;
        writeLanDiscoveryHeader(reply, LanMessageKind::EchoReply);
        ::sendto(socketFd_, reply.bytes().data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&senderAddr),
                 senderLen);
    }

    announceTimer_ += dt;
    if (announceTimer_ < kAnnounceIntervalSeconds) return;
    announceTimer_ = 0.0f;

    ByteWriter writer;
    writeLanDiscoveryHeader(writer, LanMessageKind::Announce);
    serializeLanAnnouncement(announcement, writer);

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(announcePort_);
    ::inet_pton(AF_INET, destinationAddress_.c_str(), &destAddr.sin_addr);
    ::sendto(socketFd_, writer.bytes().data(), writer.size(), 0, reinterpret_cast<sockaddr*>(&destAddr),
             sizeof(destAddr));
}

#else // !defined(__linux__)

// Kronos ("Active Joining UI"): real, honest no-op -- see class comment.
LanSessionAnnouncer::~LanSessionAnnouncer() = default;
bool LanSessionAnnouncer::start(const std::string&, uint16_t, uint16_t) { return false; }
void LanSessionAnnouncer::stop() {}
void LanSessionAnnouncer::tick(float, const LanSessionAnnouncement&) {}

#endif

} // namespace engine::net
