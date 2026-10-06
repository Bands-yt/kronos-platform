// Written against the documented IFileOpenDialog contract; this
// development environment is Linux-only, so it has not been run here.
#include "core/NativeFileDialog.hpp"

#if defined(_WIN32)

#include <windows.h>
#include <shobjidl.h>

#include <utility>

namespace engine::core {

namespace {

std::string& lastError() {
    static std::string error;
    return error;
}

std::vector<std::pair<std::function<void(const std::string&)>, std::string>>& pickedQueue() {
    static std::vector<std::pair<std::function<void(const std::string&)>, std::string>> queue;
    return queue;
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), len);
    return wide;
}

std::string toUtf8(PCWSTR wide) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string text(static_cast<size_t>(len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, text.data(), len, nullptr, nullptr);
    return text;
}

} // namespace

std::optional<std::string> openFileDialog(const FileDialogOptions& options) {
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool weInitializedCom = SUCCEEDED(comInit);

    IFileDialog* dialog = nullptr;
    HRESULT hr = options.save
                     ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
                     : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        lastError() = "Could not create the Windows file dialog.";
        if (weInitializedCom) CoUninitialize();
        return std::nullopt;
    }
    lastError().clear();

    const std::wstring title = toWide(options.title);
    dialog->SetTitle(title.c_str());

    DWORD flags = 0;
    if (SUCCEEDED(dialog->GetOptions(&flags))) {
        flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (options.save) {
            flags |= FOS_OVERWRITEPROMPT;
        } else {
            flags |= options.directory ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
        }
        dialog->SetOptions(flags);
    }

    std::wstring pattern;
    for (const std::string& ext : options.extensions) {
        if (ext.empty()) continue;
        if (!pattern.empty()) pattern += L';';
        pattern += toWide(ext);
    }
    const std::wstring filterName = toWide(options.filterName.empty() ? "Supported files" : options.filterName);
    if (!options.directory && !pattern.empty()) {
        const COMDLG_FILTERSPEC filterSpec[] = {{filterName.c_str(), pattern.c_str()}, {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filterSpec);
        dialog->SetFileTypeIndex(1);
    }

    if (options.save && !options.defaultName.empty()) {
        const std::wstring defaultName = toWide(options.defaultName);
        dialog->SetFileName(defaultName.c_str());
        if (!options.extensions.empty() && options.extensions.front().size() > 2) {
            const std::wstring extension = toWide(options.extensions.front().substr(2));
            dialog->SetDefaultExtension(extension.c_str());
        }
    }

    std::optional<std::string> result;
    hr = dialog->Show(GetActiveWindow());
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR pathW = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW))) {
                std::string path = toUtf8(pathW);
                if (!path.empty()) result = std::move(path);
                CoTaskMemFree(pathW);
            }
            item->Release();
        }
    }

    dialog->Release();
    if (weInitializedCom) CoUninitialize();
    return result;
}

std::optional<std::string> openFileDialog(const std::string& title, const std::vector<std::string>& extensions) {
    FileDialogOptions options;
    options.title = title;
    options.extensions = extensions;
    return openFileDialog(options);
}

// IFileOpenDialog::Show runs its own modal message loop, so the window
// stays responsive and the result can be delivered on the next poll.
bool openFileDialogAsync(const FileDialogOptions& options, std::function<void(const std::string&)> onPicked) {
    std::optional<std::string> path = openFileDialog(options);
    if (!lastError().empty()) return false;
    if (path && onPicked) pickedQueue().emplace_back(std::move(onPicked), std::move(*path));
    return true;
}

void pollFileDialogs() {
    auto picked = std::move(pickedQueue());
    pickedQueue().clear();
    for (auto& [callback, path] : picked) callback(path);
}

bool fileDialogOpen() { return false; }

const std::string& fileDialogError() { return lastError(); }

} // namespace engine::core

#endif
