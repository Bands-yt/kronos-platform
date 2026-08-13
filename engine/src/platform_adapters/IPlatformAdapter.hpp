#pragma once

namespace engine::platform_adapters {

enum class PlatformKind { Windows, Linux, MacOS, IOS, Android, Xbox, PlayStation, Switch };

// What a platform can physically do -- UnifiedInput (§8.2) and
// accessibility's remapping/haptics (§12) both branch on this rather than
// on PlatformKind directly, so "does this input action make sense here"
// is a capability question, not a growing if/else over every platform enum
// value.
struct PlatformCapabilities {
    bool hasKeyboardMouse = false;
    bool hasGamepad = false;
    bool hasTouch = false;
    bool hasGyro = false;
    bool hasHaptics = false;
    bool hasAdaptiveTriggers = false; // PlayStation DualSense-specific
    bool tvSafeZoneRequired = false;  // console output profile, see docs/ARCHITECTURE.md §8.3
};

// One per platform in docs/ARCHITECTURE.md §8.1's matrix. Deliberately a
// thin interface: window creation is core::Window's job, rendering is
// Renderer's, input polling is UnifiedInput's. What's left for a platform
// adapter is platform *identity and lifecycle* -- what this platform can
// do, and whatever platform-specific init/teardown its real backend needs
// (a console SDK's session bootstrap, a mobile OS's lifecycle callbacks).
//
// Every adapter here is a stub: desktop ones (Windows/Linux/macOS) report
// real capabilities since those are known statically, but none of them
// call into an actual platform SDK yet. Mobile (iOS/Android) and console
// (Xbox/PlayStation/Switch) adapters additionally cannot be implemented
// for real in this repository -- see adapters/README.md for why the
// console three specifically are structurally different from the other
// five, not just "not written yet".
class IPlatformAdapter {
public:
    virtual ~IPlatformAdapter() = default;

    [[nodiscard]] virtual PlatformKind kind() const = 0;
    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual PlatformCapabilities capabilities() const = 0;

    // False if this platform's real backend isn't available in this build
    // (true for every adapter in this skeleton -- see class comment).
    // Callers must check the return value rather than assume success;
    // a build can link every adapter but only one is ever actually usable
    // on the machine running it.
    [[nodiscard]] virtual bool initialize() = 0;
    virtual void shutdown() = 0;
};

} // namespace engine::platform_adapters
