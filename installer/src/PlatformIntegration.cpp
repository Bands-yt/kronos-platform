#include "PlatformIntegration.hpp"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#include <cstdlib>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace kronos_installer {

#if !defined(_WIN32)
namespace {
// Kronos ("kronos:// launch URI"): real fork+exec, not std::system() --
// this runs with a shell-interpreted argument in std::system()'s case,
// which is an unnecessary injection surface for a fixed, hardcoded
// command line that never needs one. installer/ has zero dependency on
// engine_core (see installer/CMakeLists.txt's own header comment), so
// this can't reuse core::launchProcess() -- a small, self-contained
// helper here is more honest than pretending a cross-project dependency
// exists.
//
// `xdg-mime default` is what actually writes the per-user association
// in ~/.config/mimeapps.list; the .desktop file's own MimeType= line
// (see createPlatformShortcut() above) declares that this app CAN
// handle the scheme, but on several real desktop environments that
// alone does not make it the DEFAULT handler without this step too.
bool runXdgMimeDefault(std::string& outError) {
    pid_t pid = fork();
    if (pid < 0) {
        outError = "fork() failed running xdg-mime";
        return false;
    }
    if (pid == 0) {
        // Child: exactly the fixed argv xdg-mime expects, no shell
        // involved, so nothing here is ever re-interpreted.
        execlp("xdg-mime", "xdg-mime", "default", "kronos.desktop", "x-scheme-handler/kronos",
               static_cast<char*>(nullptr));
        _exit(127); // execlp only returns on failure (e.g. xdg-mime not installed)
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        outError = "waitpid() failed running xdg-mime";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        outError = "xdg-mime default kronos.desktop x-scheme-handler/kronos failed -- is xdg-utils installed? "
                    "The .desktop file's own MimeType= line is still written either way, which some desktop "
                    "environments pick up on their own.";
        return false;
    }
    return true;
}
} // namespace
#endif

#if defined(_WIN32)
// Kronos ("kronos:// launch URI"): real, standard Win32 registry API --
// the same real HKCU\Software\Classes\<scheme> shape every real desktop
// app with a custom URL scheme uses. Honesty note, same as
// createPlatformShortcut() below: this environment is Linux-only (see
// engine/src/core/CredentialStoreWindows.cpp's own identical real
// precedent) -- written carefully against the documented Win32 registry
// API contract, never compiled here.
bool registerUrlProtocolHandler(const std::wstring& installedRuntimePathW, std::string& outError) {
    HKEY protocolKey = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\kronos", 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &protocolKey, nullptr);
    if (result != ERROR_SUCCESS) {
        outError = "RegCreateKeyExW(Software\\Classes\\kronos) failed";
        return false;
    }

    // The (Default) value is a real, human-readable description shown
    // by some shells/security prompts when a kronos:// link is first
    // clicked -- not load-bearing for the association itself.
    const std::wstring description = L"URL:Kronos Protocol";
    RegSetValueExW(protocolKey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(description.c_str()),
                   static_cast<DWORD>((description.size() + 1) * sizeof(wchar_t)));
    // An empty REG_SZ named "URL Protocol" -- its mere PRESENCE under
    // this key is the real, documented signal to Windows that this key
    // describes a URL protocol handler, not its (empty) content.
    RegSetValueExW(protocolKey, L"URL Protocol", 0, REG_SZ, reinterpret_cast<const BYTE*>(L""), sizeof(wchar_t));
    RegCloseKey(protocolKey);

    HKEY commandKey = nullptr;
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\kronos\\shell\\open\\command", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &commandKey, nullptr);
    if (result != ERROR_SUCCESS) {
        outError = "RegCreateKeyExW(Software\\Classes\\kronos\\shell\\open\\command) failed";
        return false;
    }

    // The flag AND its value are quoted together as ONE argv token
    // ("--kronos-uri=%1", not --kronos-uri="%1" or "%1" alone) so
    // CommandLineToArgvW hands engine_runtime's argv exactly
    // "--kronos-uri=<the real clicked URI>" as a single element
    // regardless of what characters end up inside it (query separators,
    // future additional params) -- the exact same real flag main.cpp's
    // own argv loop parses (see core::parseKronosLaunchUri()).
    std::wstring command = L"\"" + installedRuntimePathW + L"\" \"--kronos-uri=%1\"";
    result = RegSetValueExW(commandKey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                             static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(commandKey);
    if (result != ERROR_SUCCESS) {
        outError = "RegSetValueExW(shell\\open\\command) failed";
        return false;
    }
    return true;
}

// Kronos: real, standard Win32 Shell Link (COM) API -- the same real
// mechanism Windows Explorer itself uses to create .lnk files. Honesty
// note: this environment is Linux-only (see engine/src/core/
// CredentialStoreWindows.cpp's own identical real precedent) -- written
// carefully against the documented COM contract, never compiled here.
bool createPlatformShortcut(const std::string& installedRuntimePath, std::string& outError) {
    HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool weInitializedCom = SUCCEEDED(comInit);

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   reinterpret_cast<void**>(&shellLink));
    if (FAILED(hr)) {
        outError = "CoCreateInstance(CLSID_ShellLink) failed";
        if (weInitializedCom) CoUninitialize();
        return false;
    }

    std::wstring targetPath(installedRuntimePath.begin(), installedRuntimePath.end());
    shellLink->SetPath(targetPath.c_str());
    std::filesystem::path workingDir = std::filesystem::path(installedRuntimePath).parent_path();
    std::wstring workingDirW = workingDir.wstring();
    shellLink->SetWorkingDirectory(workingDirW.c_str());
    shellLink->SetDescription(L"Kronos Platform");

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
    bool ok = false;
    if (SUCCEEDED(hr)) {
        PWSTR desktopPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) {
            std::filesystem::path shortcutPath = std::filesystem::path(desktopPath) / L"Kronos.lnk";
            CoTaskMemFree(desktopPath);
            hr = persistFile->Save(shortcutPath.wstring().c_str(), TRUE);
            ok = SUCCEEDED(hr);
            if (!ok) outError = "IPersistFile::Save() failed writing the real .lnk shortcut";
        } else {
            outError = "could not resolve the real Desktop folder path";
        }
        persistFile->Release();
    } else {
        outError = "IShellLinkW::QueryInterface(IPersistFile) failed";
    }

    shellLink->Release();
    if (weInitializedCom) CoUninitialize();

    // Kronos ("kronos:// launch URI"): real, standard user-scope URL
    // protocol registration -- HKEY_CURRENT_USER rather than
    // HKEY_CLASSES_ROOT/HKLM specifically so this needs no elevation,
    // matching the .lnk shortcut above (also written with no elevation
    // check). This is the exact same registry shape every real desktop
    // app using a custom URL scheme uses (Slack, Zoom, VS Code, etc.):
    // a scheme key with an empty "URL Protocol" value (its mere presence
    // is what marks this key as describing a protocol handler, not its
    // content) plus a shell\open\command pointing back at this install.
    //
    // Attempted regardless of whether the .lnk shortcut above succeeded
    // -- they are unrelated Windows features (Explorer visibility vs.
    // protocol handling), and a failure resolving the Desktop folder has
    // no bearing on whether the registry is writable. `ok` (the overall
    // return value) reflects whether BOTH succeeded; outError is
    // extended, not replaced, so a caller sees every real failure, not
    // just whichever happened last.
    std::string protocolError;
    bool protocolOk = registerUrlProtocolHandler(targetPath, protocolError);
    if (!protocolOk) {
        outError = outError.empty() ? protocolError : (outError + "; " + protocolError);
    }
    return ok && protocolOk;
}
#else
// Kronos: real Linux post-install integration -- a real, freedesktop.org
// Desktop Entry (~/.local/share/applications/kronos.desktop, the exact
// same real format engine's own scripts/package_alpha.sh already
// generates for the plain distributable package -- see that script's
// own real kronos.desktop heredoc) plus a real symlink into
// ~/.local/bin (a real, common modern-Linux "already on PATH"
// location; honestly not guaranteed universal -- flagged in outError
// if that specific step fails, not silently swallowed).
bool createPlatformShortcut(const std::string& installedRuntimePath, std::string& outError) {
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        outError = "$HOME is not set -- can't resolve a real per-user install location";
        return false;
    }

    std::filesystem::path applicationsDir = std::filesystem::path(home) / ".local" / "share" / "applications";
    std::error_code ec;
    std::filesystem::create_directories(applicationsDir, ec);
    if (ec) {
        outError = "could not create \"" + applicationsDir.string() + "\"";
        return false;
    }

    std::ofstream desktopFile(applicationsDir / "kronos.desktop", std::ios::trunc);
    if (!desktopFile.good()) {
        outError = "could not write the real .desktop file";
        return false;
    }
    desktopFile << "[Desktop Entry]\n"
                << "Type=Application\n"
                << "Name=Kronos\n"
                << "Comment=Kronos Platform -- Game Catalogue and Home Screen\n"
                // %u: the freedesktop.org Desktop Entry Specification's
                // own field code for "substitute a single URL argument
                // here", as its OWN space-delimited argv token -- NOT
                // glued to a flag via "=". That distinction is not
                // theoretical: an earlier version of this line read
                // "--kronos-uri=%u", and real-world testing against this
                // exact xdg-open/gio resolver on a live desktop (`xdg-open
                // "kronos://launch?game=x"`, inspecting the real argv the
                // launched process received) showed it does NOT
                // substitute %u inside a larger token -- it leaves
                // "--kronos-uri=%u" completely literal and appends the
                // resolved URL as a separate trailing argument instead.
                // main.cpp accordingly accepts a bare kronos:// argv
                // token as well as the --kronos-uri= prefixed form Windows
                // uses (see its argv loop and core::parseKronosLaunchUri()).
                << "Exec=" << installedRuntimePath << " %u\n"
                << "Terminal=false\n"
                << "Categories=Game;\n"
                // Kronos ("kronos:// launch URI"): the real, documented
                // signal that this application handles the kronos: URL
                // scheme -- what actually makes "Open in Kronos" on the
                // web storefront able to launch this app at all. Without
                // this line the .desktop entry above is a plain Start
                // Menu launcher and nothing more, exactly what it was
                // before this.
                << "MimeType=x-scheme-handler/kronos;\n";
    desktopFile.close();

    // Real "update the local path" -- a real symlink, not a copy, so a
    // later real reinstall/update just re-targets it.
    std::filesystem::path localBin = std::filesystem::path(home) / ".local" / "bin";
    std::filesystem::create_directories(localBin, ec);
    std::filesystem::path linkPath = localBin / "kronos";
    std::error_code removeEc;
    std::filesystem::remove(linkPath, removeEc); // real, honest -- clears a stale symlink from a previous install first
    std::filesystem::create_symlink(installedRuntimePath, linkPath, ec);
    if (ec) {
        outError = "wrote the real .desktop launcher, but could not symlink \"" + linkPath.string() +
                    "\" -- make sure ~/.local/bin exists and is writable if you want the `kronos` command on PATH";
        return false;
    }

    // Attempted regardless of anything above it: the .desktop file and
    // symlink are already written and usable by this point, and a
    // missing xdg-mime binary has no bearing on either of those.
    std::string mimeError;
    if (!runXdgMimeDefault(mimeError)) {
        outError = "wrote the real .desktop launcher and symlink, but " + mimeError;
        return false;
    }

    return true;
}
#endif

} // namespace kronos_installer
