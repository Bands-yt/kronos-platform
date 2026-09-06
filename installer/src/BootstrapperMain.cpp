// KronosBootstrapper -- the real target of the kronos:// URI protocol
// handler (see ../build_installer.iss's own [Registry] section), not
// kronos_installer (that one is the separate GitHub-release downloader/
// updater -- see this project's own top-level CMakeLists.txt comment).
// Forwards argv[1] (the raw clicked URI, e.g.
// kronos://launch?game=<slug>&handoff=<code>) to engine_runtime, in the
// exact same --kronos-uri=<uri> form main.cpp's own argv loop already
// parses (core::parseKronosLaunchUri()) -- so the handoff code inside it
// reaches engine_runtime unchanged. Spawns and exits immediately; does
// not wait for the real app.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
constexpr const char* kRuntimeExeName = "engine_runtime.exe";
#else
constexpr const char* kRuntimeExeName = "engine_runtime";
#endif

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path selfDir = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::path runtimePath = selfDir / kRuntimeExeName;

    if (!std::filesystem::exists(runtimePath)) {
        std::fprintf(stderr, "KronosBootstrapper: %s not found next to this executable.\n", kRuntimeExeName);
        return 1;
    }

    std::string kronosUriArg;
    if (argc > 1) kronosUriArg = std::string("--kronos-uri=") + argv[1];

#if defined(_WIN32)
    std::wstring runtimePathW = runtimePath.wstring();
    std::wstring cmd = L"\"" + runtimePathW + L"\"";
    if (!kronosUriArg.empty()) {
        std::wstring argW(kronosUriArg.begin(), kronosUriArg.end());
        cmd += L" \"" + argW + L"\"";
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
    cmdBuffer.push_back(L'\0');
    std::wstring workingDir = selfDir.wstring();

    BOOL ok = CreateProcessW(runtimePathW.c_str(), cmdBuffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                              workingDir.c_str(), &startupInfo, &processInfo);
    if (!ok) {
        std::fprintf(stderr, "KronosBootstrapper: CreateProcessW failed (error %lu).\n", GetLastError());
        return 1;
    }
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("KronosBootstrapper: fork failed");
        return 1;
    }
    if (pid == 0) {
        std::vector<char*> execArgs;
        std::string runtimePathStr = runtimePath.string();
        execArgs.push_back(runtimePathStr.data());
        if (!kronosUriArg.empty()) execArgs.push_back(kronosUriArg.data());
        execArgs.push_back(nullptr);
        execv(runtimePathStr.c_str(), execArgs.data());
        _exit(127); // execv only returns on failure
    }
    return 0;
#endif
}
