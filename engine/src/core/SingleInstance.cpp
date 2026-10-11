#include "core/SingleInstance.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace engine::core {

#ifdef _WIN32

SingleInstance::~SingleInstance() {
    if (mutex_ != nullptr) CloseHandle(static_cast<HANDLE>(mutex_));
}

bool SingleInstance::acquire(const std::string& name) {
    if (mutex_ != nullptr) return true;
    std::string full = "Local\\" + name;
    HANDLE mutex = CreateMutexA(nullptr, TRUE, full.c_str());
    if (mutex == nullptr) return true; // can't tell; don't block the launch
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return false;
    }
    mutex_ = mutex;
    return true;
}

bool bringWindowToFront(const std::string& title) {
    HWND window = FindWindowA(nullptr, title.c_str());
    if (window == nullptr) return false;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    return SetForegroundWindow(window) != 0;
}

#else

SingleInstance::~SingleInstance() {
    if (fd_ >= 0) close(fd_);
}

bool SingleInstance::acquire(const std::string& name) {
    if (fd_ >= 0) return true;
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    std::string path = runtimeDir != nullptr && *runtimeDir != '\0'
                           ? std::string(runtimeDir) + "/" + name + ".lock"
                           : "/tmp/" + name + "-" + std::to_string(getuid()) + ".lock";
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return true; // can't tell; don't block the launch
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        bool held = errno == EWOULDBLOCK;
        close(fd);
        return !held;
    }
    fd_ = fd;
    return true;
}

bool bringWindowToFront(const std::string&) { return false; }

#endif

} // namespace engine::core
