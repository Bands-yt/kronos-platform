#pragma once

#include <cstddef>
#include <string>

#include "core/NativeFileDialog.hpp"

namespace engine::studio {

// "Browse..." button that fills `buffer` from a native file dialog without
// blocking the frame. Returns true on the frame the chosen path arrives.
bool browseButton(const char* id, char* buffer, size_t bufferSize, const core::FileDialogOptions& options);
bool browseButton(const char* id, std::string& value, const core::FileDialogOptions& options);

} // namespace engine::studio
