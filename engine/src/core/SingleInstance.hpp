#pragma once

#include <string>

namespace engine::core {

// A per-user lock so only one Kronos Player home screen runs at a time. Released when this object
// goes away or the process ends (even on a crash). Programs it starts don't inherit it.
class SingleInstance {
public:
    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // False when another process already holds `name`.
    bool acquire(const std::string& name);

private:
#ifdef _WIN32
    void* mutex_ = nullptr;
#else
    int fd_ = -1;
#endif
};

// Restores and focuses a top-level window with this title (Windows only; elsewhere does nothing and
// returns false, since Wayland doesn't let one app raise another's window).
bool bringWindowToFront(const std::string& title);

} // namespace engine::core
