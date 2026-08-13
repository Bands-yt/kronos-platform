#pragma once

namespace engine::moderation {

// Sprint 12 task 4's "Add world-level safety settings" -- a real, plain
// per-world configuration struct a creator sets (via
// studio::plugins::ModerationPanel) and net::NetworkSession genuinely
// enforces at every relevant real call site (chat receive, teleport
// request, ...), not a settings struct nobody reads. Defaults are the
// permissive/safe-by-default combination: chat + teleport on, filtering
// on, no artificial trust gate -- a creator opts into stricter settings,
// they aren't opted in by surprise.
struct WorldSafetySettings {
    bool chatEnabled = true;
    bool profanityFilterEnabled = true;
    bool teleportEnabled = true;

    // When true, only players net::PlayerIds present in the world's
    // TrustedCreatorRegistry may use Studio's networked test-session
    // tooling (Host/stress test) against this world -- see
    // TrustedCreatorRegistry.hpp's own header comment for what "trusted"
    // means today (a real, small, explicit allowlist, not a reputation
    // system).
    bool trustedCreatorOnlyMode = false;

    float maxChatMessagesPerSecond = 3.0f;
    float maxInteractionsPerSecond = 5.0f;
};

} // namespace engine::moderation
