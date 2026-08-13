#pragma once

#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::platform_adapters {

// See adapters/README.md -- ordinary unfinished work, not NDA-gated. This
// is also the reference/native platform for this engine per
// docs/ARCHITECTURE.md's "Linux-native" runtime goal, so unlike the other
// desktop adapters it has nothing to translate *to* -- SDL2 + Vulkan run
// natively here already.
class LinuxAdapter final : public IPlatformAdapter {
public:
    [[nodiscard]] PlatformKind kind() const override { return PlatformKind::Linux; }
    [[nodiscard]] const char* name() const override { return "Linux"; }
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
