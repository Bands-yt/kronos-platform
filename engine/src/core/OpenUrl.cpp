#include "core/OpenUrl.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <vector>
extern char** environ;
#endif

namespace engine::core {

bool openUrlInDefaultBrowser(const std::string& url) {
#if defined(_WIN32)
    // Real Win32 "open this with its default handler" call -- returns a
    // value > 32 on real success (documented ShellExecute contract).
    HINSTANCE result = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#else
    // Real posix_spawnp() (the PATH-searching variant -- xdg-open's own
    // real install location varies by distro, so this asks the OS to
    // find it, same as a shell's own PATH lookup would, but with no
    // shell involved and therefore no command-injection surface from
    // `url`'s own contents).
    std::vector<char*> argv = {const_cast<char*>("xdg-open"), const_cast<char*>(url.c_str()), nullptr};
    pid_t pid = 0;
    int spawnResult = posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv.data(), environ);
    return spawnResult == 0;
#endif
}

} // namespace engine::core
