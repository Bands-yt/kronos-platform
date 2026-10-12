#include "core/ChildProcess.hpp"

#include <chrono>
#include <cwchar>
#include <utility>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace engine::core {

ChildProcess::~ChildProcess() { stop(); }

ChildProcess::ChildProcess(ChildProcess&& other) noexcept { *this = std::move(other); }

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this == &other) return *this;
    stop();
#if defined(_WIN32)
    process_ = std::exchange(other.process_, nullptr);
#else
    pid_ = std::exchange(other.pid_, 0);
#endif
    started_ = std::exchange(other.started_, false);
    exited_ = std::exchange(other.exited_, false);
    status_ = std::exchange(other.status_, 0);
    return *this;
}

std::string ChildProcess::describe() {
    if (!started_) return "not started";
    if (running()) return "running";
#if defined(_WIN32)
    return "exited with code " + std::to_string(status_);
#else
    return status_ < 0 ? "killed by signal " + std::to_string(-status_) : "exited with code " + std::to_string(status_);
#endif
}

#if defined(_WIN32)

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// Quotes one argument the way the C runtime splits a command line.
std::wstring quote(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

} // namespace

bool ChildProcess::start(const Options& options, std::string& error) {
    stop();
    std::wstring commandLine = quote(widen(options.executable));
    for (const std::string& arg : options.args) commandLine += L" " + quote(widen(arg));

    // Environment block: ours, with the additions replacing same-named entries.
    std::wstring block;
    if (wchar_t* current = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = current; *entry != L'\0'; entry += wcslen(entry) + 1) {
            const std::wstring text(entry);
            bool replaced = false;
            for (const auto& [name, value] : options.env) {
                const std::wstring prefix = widen(name) + L"=";
                if (_wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0) replaced = true;
            }
            if (!replaced) block += text + L'\0';
        }
        FreeEnvironmentStringsW(current);
    }
    for (const auto& [name, value] : options.env) block += widen(name) + L"=" + widen(value) + L'\0';
    block += L'\0';

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    HANDLE log = INVALID_HANDLE_VALUE;
    if (!options.logFile.empty()) {
        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        log = CreateFileW(widen(options.logFile).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log == INVALID_HANDLE_VALUE) {
            error = "can't create " + options.logFile;
            return false;
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = log;
        startup.hStdError = log;
    }
    PROCESS_INFORMATION info{};
    const std::wstring directory = widen(options.workingDirectory);
    const BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, log != INVALID_HANDLE_VALUE,
                                   CREATE_UNICODE_ENVIRONMENT, block.data(),
                                   directory.empty() ? nullptr : directory.c_str(), &startup, &info);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) {
        error = "couldn't start " + options.executable + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    started_ = true;
    exited_ = false;
    status_ = 0;
    return true;
}

bool ChildProcess::running() {
    if (!started_ || exited_) return false;
    if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return true;
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    status_ = static_cast<int>(code);
    exited_ = true;
    return false;
}

void ChildProcess::stop(int graceMs) {
    if (!started_) return;
    (void)graceMs; // Windows has no polite stop for a process without its window handle
    if (running()) {
        TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 5000);
        (void)running();
    }
    CloseHandle(process_);
    process_ = nullptr;
    started_ = false;
}

#else

bool ChildProcess::start(const Options& options, std::string& error) {
    stop();
    std::vector<std::string> envText;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string text(*entry);
        bool replaced = false;
        for (const auto& [name, value] : options.env) {
            if (text.size() > name.size() && text.compare(0, name.size(), name) == 0 && text[name.size()] == '=') replaced = true;
        }
        if (!replaced) envText.push_back(text);
    }
    for (const auto& [name, value] : options.env) envText.push_back(name + "=" + value);
    std::vector<char*> envp;
    for (std::string& text : envText) envp.push_back(text.data());
    envp.push_back(nullptr);

    std::vector<std::string> argText = {options.executable};
    argText.insert(argText.end(), options.args.begin(), options.args.end());
    std::vector<char*> argv;
    for (std::string& text : argText) argv.push_back(text.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (!options.logFile.empty()) {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, options.logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                         0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    }
    if (!options.workingDirectory.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, options.workingDirectory.c_str());
    }
    pid_t pid = 0;
    const int result = posix_spawnp(&pid, options.executable.c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        error = "couldn't start " + options.executable + " (" + std::strerror(result) + ")";
        return false;
    }
    pid_ = pid;
    started_ = true;
    exited_ = false;
    status_ = 0;
    return true;
}

bool ChildProcess::running() {
    if (!started_ || exited_) return false;
    int status = 0;
    const pid_t done = waitpid(pid_, &status, WNOHANG);
    if (done == 0) return true;
    exited_ = true;
    if (done < 0) {
        status_ = 0;
    } else if (WIFSIGNALED(status)) {
        status_ = -WTERMSIG(status);
    } else {
        status_ = WEXITSTATUS(status);
    }
    return false;
}

void ChildProcess::stop(int graceMs) {
    if (!started_) return;
    if (running()) {
        kill(pid_, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
        while (running() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (running()) {
            kill(pid_, SIGKILL);
            int status = 0;
            waitpid(pid_, &status, 0);
            exited_ = true;
            status_ = -SIGKILL;
        }
    }
    started_ = false;
    pid_ = 0;
}

#endif

} // namespace engine::core
