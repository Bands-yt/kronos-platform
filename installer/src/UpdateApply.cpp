#include "UpdateApply.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace kronos_installer {

bool waitForProcessExit(int64_t pid, double timeoutSeconds) {
    if (pid <= 0) return true; // nothing real to wait for

    auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);

#if defined(_WIN32)
    // SYNCHRONIZE is the real minimum right needed to wait on it; this
    // deliberately does not ask for anything stronger.
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        // A real "can't open it" here overwhelmingly means the process is
        // already gone (ERROR_INVALID_PARAMETER for a dead pid), which is
        // exactly the state this function exists to wait for.
        return true;
    }
    DWORD timeoutMs = static_cast<DWORD>(timeoutSeconds * 1000.0);
    DWORD waited = WaitForSingleObject(process, timeoutMs);
    CloseHandle(process);
    return waited == WAIT_OBJECT_0;
#else
    // Real POSIX: signal 0 performs the existence/permission check
    // without ever delivering a signal. ESRCH is the real "no such
    // process" answer -- the one we're waiting for.
    while (std::chrono::steady_clock::now() < deadline) {
        if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH;
#endif
}

SwapResult swapInstallDirectory(const std::string& installDir, const std::string& stagedDir,
                                 std::string& backupDirOut) {
    SwapResult result;
    backupDirOut.clear();

    std::error_code ec;
    std::filesystem::path install(installDir);
    std::filesystem::path staged(stagedDir);

    if (!std::filesystem::exists(staged, ec)) {
        result.error = "the staged update directory \"" + stagedDir + "\" does not exist";
        return result;
    }

    // A real first-time layout (nothing installed yet) needs no backup or
    // rollback at all -- just move the staged tree into place.
    bool hadExistingInstall = std::filesystem::exists(install, ec);

    std::filesystem::path backup =
        install.parent_path() / (install.filename().string() + ".old-" +
                                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    if (hadExistingInstall) {
        std::filesystem::rename(install, backup, ec);
        if (ec) {
            result.error = "could not move the existing install aside (" + ec.message() +
                           ") -- is Kronos still running, or is a file in it open elsewhere?";
            return result;
        }
    }

    std::filesystem::rename(staged, install, ec);
    if (ec) {
        // Real rollback: put the original install back exactly where it
        // was, so a failed update leaves a working app rather than none.
        if (hadExistingInstall) {
            std::error_code rollbackEc;
            std::filesystem::rename(backup, install, rollbackEc);
            result.rolledBack = !rollbackEc;
        }
        result.error = "could not move the updated files into place (" + ec.message() + ")";
        return result;
    }

    if (hadExistingInstall) backupDirOut = backup.string();
    result.success = true;
    return result;
}

bool launchDetached(const std::string& executablePath, std::string& outError) {
#if defined(_WIN32)
    STARTUPINFOA startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo;
    ZeroMemory(&processInfo, sizeof(processInfo));

    // CreateProcessA's own command-line argument is writable-in-place, so
    // it needs a real mutable buffer rather than the string's own storage.
    std::string commandLine = "\"" + executablePath + "\"";
    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');

    // The working directory is set to the executable's own folder so the
    // relaunched app resolves its real shaders/assets the same way a
    // normal launch does.
    std::string workingDir = std::filesystem::path(executablePath).parent_path().string();

    BOOL created = CreateProcessA(executablePath.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE,
                                   DETACHED_PROCESS, nullptr, workingDir.empty() ? nullptr : workingDir.c_str(),
                                   &startupInfo, &processInfo);
    if (!created) {
        outError = "CreateProcess failed (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    // Closing both handles does not terminate the real child -- it just
    // gives up this process's own references to it, which is exactly what
    // "detached" means here.
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    return true;
#else
    pid_t pid = 0;
    char* argv[] = {const_cast<char*>(executablePath.c_str()), nullptr};
    int status = ::posix_spawn(&pid, executablePath.c_str(), nullptr, nullptr, argv, environ);
    if (status != 0) {
        outError = "posix_spawn failed (" + std::string(std::strerror(status)) + ")";
        return false;
    }
    return true;
#endif
}

} // namespace kronos_installer
