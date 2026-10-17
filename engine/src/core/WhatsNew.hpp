#pragma once

#include <string>
#include <vector>

namespace engine::core {

struct WhatsNewEntry {
    std::string heading;
    std::vector<std::string> items;
};

// Shown in Studio under Help > What's New. Newest first; edit this when something creators will notice changes.
inline const std::vector<WhatsNewEntry>& whatsNew() {
    static const std::vector<WhatsNewEntry> entries = {
        {"Studio",
         {"F1 lists the keyboard shortcuts.",
          "F10 hides the side panels (focus mode). Shift+F10 also hides the ribbon for recording; Esc brings it back.",
          "View > Density makes rows tighter or roomier.",
          "Python scripts: start the script with a # line. Rust plugins: File > Rust Plugins."}},
        {"Playing and testing",
         {"Test > Server runs a real server with 1 to 4 players on your PC.",
          "Other players now appear in your game with a body.",
          "engine_runtime --gpu-info tells you what your graphics card can do."}},
        {"Moving from Roblox",
         {"Imports keep tags, welds, unions and most GUI. The import report lists what was left out.",
          "Back up your project before opening it in a newer Kronos."}},
    };
    return entries;
}

} // namespace engine::core
