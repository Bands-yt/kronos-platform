#pragma once

#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::platform_adapters {

// See adapters/README.md and XboxAdapter.hpp's comment -- same NDA-gated
// situation, for Sony's PS5 SDK. capabilities() reports DualSense's public
// hardware features (adaptive triggers, haptics); initialize() always
// fails since there is no real PS5 SDK bootstrap in this open repository.
class PlayStationAdapter final : public IPlatformAdapter {
public:
    [[nodiscard]] PlatformKind kind() const override { return PlatformKind::PlayStation; }
    [[nodiscard]] const char* name() const override { return "PlayStation"; }
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
