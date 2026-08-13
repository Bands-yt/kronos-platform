#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::anticheat {

// docs/ARCHITECTURE.md §11: "a hashed hardware/OS/driver fingerprint for
// ban-evasion detection, reused as a fraud signal (§09). Explicit
// boundary: hash-based, documented in the privacy policy, never
// repurposed for ad tracking."
//
// That boundary is enforced by *shape*, not just policy: this type only
// ever exposes an opaque hash, never the raw component strings it was
// built from. There is deliberately no accessor that returns the inputs
// back out.
struct DeviceFingerprint {
    uint64_t hash = 0;

    [[nodiscard]] std::string toHex() const;
};

// Collects a handful of stable-but-non-identifying signals (OS name,
// CPU core count, GPU device name -- already visible to any Vulkan app via
// vkEnumeratePhysicalDevices, nothing new) and folds them into one hash.
// This is intentionally coarse: the goal per §11/§09 is correlating
// "probably the same machine" across accounts for ban-evasion/fraud
// review, not uniquely re-identifying a person the way a browser
// fingerprinting library would. No MAC addresses, disk serials, or other
// hardware IDs are collected -- those would cross the governance boundary
// the header comment above describes.
class DeviceFingerprintCollector {
public:
    void addComponent(const std::string& component) { components_.push_back(component); }

    [[nodiscard]] DeviceFingerprint compute() const;

private:
    std::vector<std::string> components_;
};

} // namespace engine::anticheat
