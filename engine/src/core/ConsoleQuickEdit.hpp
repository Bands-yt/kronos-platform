#pragma once

namespace engine::core {

// Disables Windows console QuickEdit mode, which otherwise freezes the
// process on a click-drag in the console window. No-op on other platforms.
void disableConsoleQuickEditMode();

} // namespace engine::core
