#pragma once

#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::platform_adapters {

// See adapters/README.md -- ordinary unfinished work, not NDA-gated.
// Real backend would wrap XInput/DirectInput-class APIs for anything
// UnifiedInput's SDL2-based polling doesn't already cover (e.g. Xbox
// controller impulse triggers), and Win32 lifecycle/session integration.
class WindowsAdapter final : public IPlatformAdapter {
public:
    [[nodiscard]] PlatformKind kind() const override { return PlatformKind::Windows; }
    [[nodiscard]] const char* name() const override { return "Windows"; }
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
