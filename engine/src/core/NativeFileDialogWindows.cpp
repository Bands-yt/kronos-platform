// Unconditionally listed in src/CMakeLists.txt, real implementation
// guarded by #if defined(_WIN32) below -- same shape as
// CredentialStoreLinux.cpp/CredentialStoreWindows.cpp.
//
// Honesty note (same as CredentialStoreWindows.cpp/
// PlatformIntegration.cpp's own IShellLinkW code): real, standard
// IFileOpenDialog COM API usage, written against the documented
// contract -- this development environment is Linux-only, so this file
// has never actually been compiled or run here.
#include "core/NativeFileDialog.hpp"

#if defined(_WIN32)

#include <windows.h>
#include <shobjidl.h>

namespace engine::core {

std::optional<std::string> openFileDialog(const std::string& title, const std::vector<std::string>& extensions) {
    HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool weInitializedCom = SUCCEEDED(comInit);

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr =
        CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void**)&dialog);
    if (FAILED(hr)) {
        if (weInitializedCom) CoUninitialize();
        return std::nullopt;
    }

    std::wstring titleW(title.begin(), title.end());
    dialog->SetTitle(titleW.c_str());

    // A single filter entry covering every real extension the caller
    // asked for -- "*.gltf *.glb *.obj *.fbx" style, same shape zenity's
    // own --file-filter takes on the Linux side.
    std::wstring pattern;
    for (const std::string& ext : extensions) {
        if (!pattern.empty()) pattern += L' ';
        pattern += std::wstring(ext.begin(), ext.end());
    }
    if (!pattern.empty()) {
        COMDLG_FILTERSPEC filterSpec[] = {{L"Supported files", pattern.c_str()}};
        dialog->SetFileTypes(1, filterSpec);
    }

    hr = dialog->Show(nullptr);
    if (FAILED(hr)) {
        // Real, honest cancel -- HRESULT_FROM_WIN32(ERROR_CANCELLED) is
        // what Show() returns when the user closes the dialog.
        dialog->Release();
        if (weInitializedCom) CoUninitialize();
        return std::nullopt;
    }

    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    std::optional<std::string> result;
    if (SUCCEEDED(hr)) {
        PWSTR pathW = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW))) {
            int len = WideCharToMultiByte(CP_UTF8, 0, pathW, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string path(static_cast<size_t>(len) - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, pathW, -1, path.data(), len, nullptr, nullptr);
                result = std::move(path);
            }
            CoTaskMemFree(pathW);
        }
        item->Release();
    }

    dialog->Release();
    if (weInitializedCom) CoUninitialize();
    return result;
}

} // namespace engine::core

#endif
