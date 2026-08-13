#pragma once

#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::platform_adapters {

// See adapters/README.md and XboxAdapter.hpp's comment -- same NDA-gated
// situation, for Nintendo's NX SDK. capabilities() reports Switch's public
// hardware features (handheld touch, Joy-Con gyro/haptics); initialize()
// always fails since there is no real NX SDK bootstrap in this open
// repository.
class SwitchAdapter final : public IPlatformAdapter {
public:
    [[nodiscard]] PlatformKind kind() const override { return PlatformKind::Switch; }
    [[nodiscard]] const char* name() const override { return "Nintendo Switch"; }
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
