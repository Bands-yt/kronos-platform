#pragma once

#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::platform_adapters {

// See adapters/README.md: this is the NDA-gated case, not ordinary
// unfinished work. The Microsoft GDK is only available to registered
// developers and cannot be built against or redistributed from this open
// repository. capabilities() reports what Xbox hardware can do (that much
// is public information), but initialize() always fails -- there is no
// real GDK bootstrap here, on purpose, and there never will be in this
// repo. A real implementation lives in a separate, access-controlled
// repo/submodule compiled only inside Microsoft's toolchain.
class XboxAdapter final : public IPlatformAdapter {
public:
    [[nodiscard]] PlatformKind kind() const override { return PlatformKind::Xbox; }
    [[nodiscard]] const char* name() const override { return "Xbox"; }
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
