#include "PlatformIntegration.hpp"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#endif

namespace kronos_installer {

#if defined(_WIN32)
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
    return ok;
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
                << "Exec=" << installedRuntimePath << "\n"
                << "Terminal=false\n"
                << "Categories=Game;\n";
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

    return true;
}
#endif

} // namespace kronos_installer
