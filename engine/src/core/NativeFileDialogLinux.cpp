// Unconditionally listed in src/CMakeLists.txt, real implementation
// guarded by #if defined(__linux__) below -- same shape as
// CredentialStoreLinux.cpp/CredentialStoreWindows.cpp.
#include "core/NativeFileDialog.hpp"

#if defined(__linux__)

#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

namespace engine::core {

// Real fork+exec+pipe, not popen()/system() -- a real argv array, no
// shell involved, matching PlatformIntegration.cpp's own
// runXdgMimeDefault() precedent.
std::optional<std::string> openFileDialog(const std::string& title, const std::vector<std::string>& extensions) {
    int pipeFds[2];
    if (pipe(pipeFds) != 0) return std::nullopt;

    std::string filterArg;
    if (!extensions.empty()) {
        filterArg = "--file-filter=";
        for (const std::string& ext : extensions) {
            filterArg += ext;
            filterArg += ' ';
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        return std::nullopt;
    }
    if (pid == 0) {
        close(pipeFds[0]);
        dup2(pipeFds[1], STDOUT_FILENO);
        close(pipeFds[1]);
        if (!filterArg.empty()) {
            execlp("zenity", "zenity", "--file-selection", ("--title=" + title).c_str(), filterArg.c_str(),
                   static_cast<char*>(nullptr));
        } else {
            execlp("zenity", "zenity", "--file-selection", ("--title=" + title).c_str(),
                   static_cast<char*>(nullptr));
        }
        _exit(127); // zenity not installed
    }

    close(pipeFds[1]);
    std::string result;
    char buffer[512];
    ssize_t n;
    while ((n = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
        result.append(buffer, static_cast<size_t>(n));
    }
    close(pipeFds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || result.empty()) return std::nullopt;
    return result;
}

} // namespace engine::core

#endif
