#include "core/ConsoleQuickEdit.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace engine::core {

#ifdef _WIN32

void disableConsoleQuickEditMode() {
    HANDLE stdIn = GetStdHandle(STD_INPUT_HANDLE);
    if (stdIn == INVALID_HANDLE_VALUE || stdIn == nullptr) return;

    DWORD mode = 0;
    if (!GetConsoleMode(stdIn, &mode)) return;

    // ENABLE_EXTENDED_FLAGS must be set for SetConsoleMode to honor a
    // QuickEdit change at all.
    mode &= ~static_cast<DWORD>(ENABLE_QUICK_EDIT_MODE);
    mode |= ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(stdIn, mode);
}

#else

void disableConsoleQuickEditMode() {}

#endif

} // namespace engine::core
