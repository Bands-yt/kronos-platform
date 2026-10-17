// Windows half of SandboxProcess, included by PluginSandbox.cpp.
// The plugin runs in an AppContainer (no file, registry or network access beyond a private
// staging folder), inside a job object (one process, memory cap, killed with the editor),
// with process mitigation policies on. Frames travel over a private duplex named pipe.

#ifndef PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES
#define PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES 0x00020009
#endif
#ifndef PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY
#define PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY 0x0002000E
#endif
#ifndef PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY
#define PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY 0x00020007
#endif

namespace {

constexpr wchar_t kProfileName[] = L"KronosPluginSandbox";

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(size > 0 ? size - 1 : 0), L'\0');
    if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
    return out;
}

std::string windowsError(DWORD code) {
    return "Windows error " + std::to_string(code);
}

// The shared per-user AppContainer identity (created once, reused by every plugin).
PSID containerSid() {
    PSID sid = nullptr;
    const HRESULT made = CreateAppContainerProfile(kProfileName, L"Kronos plugin sandbox",
                                                   L"Runs third-party Kronos plugins", nullptr, 0, &sid);
    if (SUCCEEDED(made)) return sid;
    if (made == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) &&
        SUCCEEDED(DeriveAppContainerSidFromAppContainerName(kProfileName, &sid))) {
        return sid;
    }
    return nullptr;
}

bool grantContainerAccess(const std::wstring& folder, PSID sid) {
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    PACL oldAcl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(folder.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl,
                              nullptr, &descriptor) != ERROR_SUCCESS) {
        return false;
    }
    PACL newAcl = nullptr;
    bool ok = SetEntriesInAclW(1, &access, oldAcl, &newAcl) == ERROR_SUCCESS &&
              SetNamedSecurityInfoW(const_cast<LPWSTR>(folder.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                    nullptr, nullptr, newAcl, nullptr) == ERROR_SUCCESS;
    if (newAcl) LocalFree(newAcl);
    LocalFree(descriptor);
    return ok;
}

} // namespace

struct SandboxProcess::Native {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    ipc::Pipe pipe;
    std::wstring stageDir;
    bool alive() const { return process != nullptr; }
};

bool SandboxProcess::supported() { return true; }

bool SandboxProcess::running() const { return native_ && native_->alive(); }

bool SandboxProcess::connected() const { return native_ && native_->alive(); }

bool SandboxProcess::sendTo(ipc::Msg type, const std::vector<uint8_t>& payload, ipc::Passed passed) {
    return ipc::sendFrame(native_->pipe, type, payload, passed);
}

ipc::Recv SandboxProcess::recvFrom(ipc::Msg& type, std::vector<uint8_t>& payload, int timeoutMs) {
    return ipc::recvFrame(native_->pipe, type, payload, timeoutMs);
}

bool SandboxProcess::spawn(const std::string& executable, const std::string& pluginPath, std::string& error) {
    terminate();
    exitDescription_.clear();
    native_ = std::make_unique<Native>();
    Native& n = *native_;
    auto fail = [&](const std::string& why) {
        error = why;
        reap(true);
        return false;
    };

    PSID sid = containerSid();
    if (!sid) return fail("could not create the plugin sandbox identity");

    // A private folder the container may read: the sandbox program and a copy of the plugin.
    static std::atomic<unsigned> counter{0};
    wchar_t tempPath[MAX_PATH + 1];
    const DWORD tempLen = GetTempPathW(MAX_PATH, tempPath);
    if (tempLen == 0 || tempLen > MAX_PATH) {
        FreeSid(sid);
        return fail("could not find the temp folder");
    }
    const std::wstring stamp = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(counter++);
    n.stageDir = std::wstring(tempPath) + L"kronos-sandbox-" + stamp;
    std::error_code ec;
    std::filesystem::create_directories(n.stageDir, ec);
    if (ec || !grantContainerAccess(n.stageDir, sid)) {
        LocalFree(sid);
        return fail("could not prepare the plugin staging folder");
    }
    const std::filesystem::path stagedExe = std::filesystem::path(n.stageDir) / L"kronos_plugin_sandbox.exe";
    const std::filesystem::path stagedPlugin = std::filesystem::path(n.stageDir) / L"plugin.dll";
    std::filesystem::copy_file(std::filesystem::path(widen(executable)), stagedExe, ec);
    if (!ec) std::filesystem::copy_file(std::filesystem::path(widen(pluginPath)), stagedPlugin, ec);
    if (ec) {
        LocalFree(sid);
        return fail("could not copy the plugin into its sandbox: " + ec.message());
    }

    // The editor's pipe end is overlapped; the plugin gets the other end as an inherited handle.
    const std::wstring pipeName = L"\\\\.\\pipe\\kronos-sandbox-" + stamp;
    n.pipe.handle = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                     1u << 20, 1u << 20, 0, nullptr);
    if (n.pipe.handle == INVALID_HANDLE_VALUE) {
        n.pipe.handle = nullptr;
        LocalFree(sid);
        return fail("could not create the plugin pipe: " + windowsError(GetLastError()));
    }
    n.pipe.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
    HANDLE clientEnd = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &inheritable, OPEN_EXISTING, 0, nullptr);
    if (clientEnd == INVALID_HANDLE_VALUE) {
        LocalFree(sid);
        return fail("could not open the plugin pipe: " + windowsError(GetLastError()));
    }

    // Process attributes: the one inherited handle, the container, mitigations, no children.
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 4, 0, &attributeBytes);
    std::vector<char> attributeStorage(attributeBytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    InitializeProcThreadAttributeList(attributes, 4, 0, &attributeBytes);

    HANDLE inheritList[1] = {clientEnd};
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = sid;

    DWORD64 mitigations = 0x1ull |        // DEP
                          0x1ull << 24 |  // strict handle checks
                          0x1ull << 28 |  // Win32k system calls disabled
                          0x1ull << 32 |  // extension points disabled
                          0x1ull << 52 |  // no images from remote shares
                          0x1ull << 56;   // no low-integrity images
    DWORD childPolicy = 0x00000001; // PROCESS_CREATION_CHILD_PROCESS_RESTRICTED
    bool attributesOk =
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritList, sizeof(inheritList), nullptr, nullptr) &&
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities, sizeof(capabilities), nullptr, nullptr) &&
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &mitigations, sizeof(mitigations), nullptr, nullptr) &&
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &childPolicy, sizeof(childPolicy), nullptr, nullptr);
    if (!attributesOk) {
        const DWORD code = GetLastError();
        DeleteProcThreadAttributeList(attributes);
        CloseHandle(clientEnd);
        LocalFree(sid);
        return fail("could not set up the plugin sandbox: " + windowsError(code));
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    std::wstring commandLine = L"\"" + stagedExe.wstring() + L"\" " + std::to_wstring(reinterpret_cast<uintptr_t>(clientEnd)) +
                               L" \"" + stagedPlugin.wstring() + L"\"";
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(stagedExe.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                                        n.stageDir.c_str(), &startup.StartupInfo, &info);
    const DWORD createError = GetLastError();
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(clientEnd);
    LocalFree(sid);
    if (!created) return fail("could not start the plugin sandbox: " + windowsError(createError));
    n.process = info.hProcess;
    n.pipe.peerProcess = info.hProcess;

    // One process, a memory cap, and it dies with the editor.
    n.job = CreateJobObjectW(nullptr, nullptr);
    bool jobOk = n.job != nullptr;
    if (jobOk) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                                                  JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(1024) * 1024 * 1024;
        jobOk = SetInformationJobObject(n.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
                AssignProcessToJobObject(n.job, info.hProcess);
        JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
        ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
                                 JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
                                 JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
                                 JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
        if (jobOk) SetInformationJobObject(n.job, JobObjectBasicUIRestrictions, &ui, sizeof(ui));
    }
    if (!jobOk) {
        const DWORD code = GetLastError();
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        return fail("could not limit the plugin process: " + windowsError(code));
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    return true;
}

void SandboxProcess::reap(bool kill) {
    if (!native_) return;
    Native& n = *native_;
    if (n.process) {
        if (kill) TerminateProcess(n.process, 1);
        WaitForSingleObject(n.process, 5000);
        DWORD code = 0;
        if (GetExitCodeProcess(n.process, &code) && exitDescription_.empty()) {
            if (code == 0xC0000005) {
                exitDescription_ = "crashed (Segmentation fault)";
            } else if (code >= 0xC0000000) {
                char hex[32];
                std::snprintf(hex, sizeof(hex), "0x%08lX", static_cast<unsigned long>(code));
                exitDescription_ = std::string("crashed (exception ") + hex + ")";
            } else {
                exitDescription_ = "exited with code " + std::to_string(code);
            }
        }
        CloseHandle(n.process);
        n.process = nullptr;
        n.pipe.peerProcess = nullptr;
    }
    if (n.pipe.handle) CloseHandle(n.pipe.handle);
    if (n.pipe.event) CloseHandle(n.pipe.event);
    if (n.job) CloseHandle(n.job);
    if (!n.stageDir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(n.stageDir, ec);
    }
    native_.reset();
}

void SandboxProcess::terminate() { reap(true); }
