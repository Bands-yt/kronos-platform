#pragma once

#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::platform_adapters {

// See adapters/README.md -- ordinary unfinished work, not NDA-gated.
// Real backend runs Vulkan via MoltenVK (docs/ARCHITECTURE.md §3/§8.1) --
// this adapter is where any Cocoa-level lifecycle glue MoltenVK doesn't
// already handle through SDL2 would attach.
class MacOSAdapter final : public IPlatformAdapter {
public:
    [[nodiscard]] PlatformKind kind() const override { return PlatformKind::MacOS; }
    [[nodiscard]] const char* name() const override { return "macOS"; }
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
