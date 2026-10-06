#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace engine::core {

struct FileDialogOptions {
    std::string title = "Open";
    // Glob patterns such as "*.gltf". Empty means any file. An "All files"
    // entry is always offered as well.
    std::vector<std::string> extensions;
    std::string filterName = "Supported files";
    bool directory = false;
    // Save mode: asks for a new path (with an overwrite prompt) instead of
    // an existing file; defaultName pre-fills the file name.
    bool save = false;
    std::string defaultName;
};

// Blocking native "Open File" dialog. Returns nullopt on cancel or when no
// dialog backend is available.
[[nodiscard]] std::optional<std::string> openFileDialog(const std::string& title,
                                                          const std::vector<std::string>& extensions);
[[nodiscard]] std::optional<std::string> openFileDialog(const FileDialogOptions& options);

// Non-blocking variant for UI code: the app keeps rendering while the
// dialog is up. `onPicked` runs from pollFileDialogs() on the calling
// thread, only when the user actually chose something. Returns false when
// no dialog could be started.
bool openFileDialogAsync(const FileDialogOptions& options, std::function<void(const std::string&)> onPicked);

// Call once per frame from the UI thread.
void pollFileDialogs();

[[nodiscard]] bool fileDialogOpen();

// Why the last dialog failed to start or show, empty if it didn't.
[[nodiscard]] const std::string& fileDialogError();

} // namespace engine::core
