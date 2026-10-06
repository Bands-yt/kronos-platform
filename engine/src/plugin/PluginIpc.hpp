#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

// Messages between the engine and a sandboxed plugin process. The engine
// sends a request and services the plugin's host calls until the reply
// arrives, so only one side runs at a time.
namespace engine::plugin::ipc {

enum class Msg : uint8_t {
    Load = 1, // engine -> sandbox
    Start,
    Tick,
    Draw,
    Import,
    Unload,

    Reply = 32, // sandbox -> engine
    Log,
    RegisterImporter,
    RegisterPanel,
    OpenChannel,
    EntityCount,
    EntityAt,
    FindEntity,
    EntityName,
    GetPosition,
    SetPosition,

    HostReply = 64, // engine -> sandbox, answering a host call
};

constexpr uint32_t kMaxFrameBytes = 256u * 1024u * 1024u;
constexpr uint32_t kMaxUiCommands = 4096;

enum class UiKind : uint8_t { Text = 1, Button, Checkbox, Slider, InputText, Separator, SameLine };

struct Writer {
    std::vector<uint8_t> bytes;

    template <typename T>
    Writer& put(T value) {
        const auto* raw = reinterpret_cast<const uint8_t*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(T));
        return *this;
    }
    Writer& str(const std::string& value) { return blob(value.data(), value.size()); }
    Writer& blob(const void* data, size_t size) {
        put(static_cast<uint32_t>(size));
        const auto* raw = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), raw, raw + size);
        return *this;
    }
};

struct Reader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t at = 0;
    bool ok = true;

    explicit Reader(const std::vector<uint8_t>& bytes) : data(bytes.data()), size(bytes.size()) {}
    explicit Reader(std::vector<uint8_t>&&) = delete;

    template <typename T>
    T get() {
        T value{};
        if (!ok || size - at < sizeof(T)) {
            ok = false;
            return value;
        }
        std::memcpy(&value, data + at, sizeof(T));
        at += sizeof(T);
        return value;
    }
    std::string str(uint32_t limit = 1u << 20) {
        const auto length = get<uint32_t>();
        if (!ok || length > limit || size - at < length) {
            ok = false;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(data + at), length);
        at += length;
        return value;
    }
    bool blob(std::vector<uint8_t>& out) {
        const auto length = get<uint32_t>();
        if (!ok || size - at < length) return ok = false;
        out.assign(data + at, data + at + length);
        at += length;
        return true;
    }
};

inline uint32_t labelHash(const char* label) {
    uint32_t hash = 2166136261u;
    for (const char* c = label; c && *c; ++c) hash = (hash ^ static_cast<uint8_t>(*c)) * 16777619u;
    return hash;
}

#if defined(__linux__)

enum class Recv { Ok, Closed, Timeout, Error };

inline bool writeAll(int fd, const uint8_t* data, size_t size, int passFd) {
    bool fdSent = passFd < 0;
    while (size > 0) {
        iovec iov{const_cast<uint8_t*>(data), size};
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))] = {};
        if (!fdSent) {
            msg.msg_control = control;
            msg.msg_controllen = sizeof(control);
            cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int));
            std::memcpy(CMSG_DATA(cmsg), &passFd, sizeof(int));
        }
        const ssize_t sent = sendmsg(fd, &msg, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        fdSent = true;
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

inline bool sendFrame(int fd, Msg type, const std::vector<uint8_t>& payload, int passFd = -1) {
    std::vector<uint8_t> frame(5 + payload.size());
    const auto length = static_cast<uint32_t>(payload.size() + 1);
    std::memcpy(frame.data(), &length, 4);
    frame[4] = static_cast<uint8_t>(type);
    if (!payload.empty()) std::memcpy(frame.data() + 5, payload.data(), payload.size());
    return writeAll(fd, frame.data(), frame.size(), passFd);
}

// deadlineMs < 0 waits forever. A received descriptor (at most one per
// frame) lands in *receivedFd; extra ones are closed.
inline Recv readAll(int fd, uint8_t* data, size_t size, int timeoutMs, int* receivedFd) {
    while (size > 0) {
        if (timeoutMs >= 0) {
            pollfd pfd{fd, POLLIN, 0};
            const int ready = ::poll(&pfd, 1, timeoutMs);
            if (ready == 0) return Recv::Timeout;
            if (ready < 0) {
                if (errno == EINTR) continue;
                return Recv::Error;
            }
        }
        iovec iov{data, size};
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 4)] = {};
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        const ssize_t got = recvmsg(fd, &msg, MSG_CMSG_CLOEXEC);
        if (got == 0) return Recv::Closed;
        if (got < 0) {
            if (errno == EINTR) continue;
            return Recv::Error;
        }
        for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) continue;
            const size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < count; ++i) {
                int passed = -1;
                std::memcpy(&passed, CMSG_DATA(cmsg) + i * sizeof(int), sizeof(int));
                if (receivedFd && *receivedFd < 0) {
                    *receivedFd = passed;
                } else {
                    ::close(passed);
                }
            }
        }
        data += got;
        size -= static_cast<size_t>(got);
    }
    return Recv::Ok;
}

inline Recv recvFrame(int fd, Msg& type, std::vector<uint8_t>& payload, int timeoutMs, int* receivedFd = nullptr) {
    uint8_t header[5];
    if (receivedFd) *receivedFd = -1;
    Recv status = readAll(fd, header, sizeof(header), timeoutMs, receivedFd);
    if (status != Recv::Ok) return status;
    uint32_t length = 0;
    std::memcpy(&length, header, 4);
    if (length == 0 || length > kMaxFrameBytes) return Recv::Error;
    type = static_cast<Msg>(header[4]);
    payload.resize(length - 1);
    if (payload.empty()) return Recv::Ok;
    return readAll(fd, payload.data(), payload.size(), timeoutMs, nullptr);
}

#endif

} // namespace engine::plugin::ipc
