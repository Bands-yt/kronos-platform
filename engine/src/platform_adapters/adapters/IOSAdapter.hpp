#pragma once

#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::platform_adapters {

// See adapters/README.md -- publicly available SDK (Xcode), stubbed for
// scope reasons, not NDA ones. A real backend needs UIKit app-lifecycle
// integration (UIApplicationDelegate callbacks for background/foreground
// transitions) that this skeleton doesn't attempt.
class IOSAdapter final : public IPlatformAdapter {
public:
    [[nodiscard]] PlatformKind kind() const override { return PlatformKind::IOS; }
    [[nodiscard]] const char* name() const override { return "iOS"; }
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
