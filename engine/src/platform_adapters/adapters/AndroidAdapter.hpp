#pragma once

#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::platform_adapters {

// See adapters/README.md -- publicly available SDK (Android NDK), stubbed
// for scope reasons, not NDA ones. A real backend needs ANativeActivity
// lifecycle integration, which this skeleton doesn't attempt.
class AndroidAdapter final : public IPlatformAdapter {
public:
    [[nodiscard]] PlatformKind kind() const override { return PlatformKind::Android; }
    [[nodiscard]] const char* name() const override { return "Android"; }
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
