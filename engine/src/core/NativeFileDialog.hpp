#pragma once

#include <optional>
#include <string>
#include <vector>

namespace engine::core {

// Real, native "Open File" dialog -- blocks the calling thread until the
// user picks a file or cancels, same as every OS's own native file
// dialog call already does. `extensions` is a list of real glob patterns
// (e.g. "*.gltf") shown as the dialog's own filter; empty means "any
// file". Returns nullopt on cancel or if no real dialog backend was
// available (never a fabricated path).
[[nodiscard]] std::optional<std::string> openFileDialog(const std::string& title,
                                                          const std::vector<std::string>& extensions);

} // namespace engine::core
