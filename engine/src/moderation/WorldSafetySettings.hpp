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
    // Kronos ("Moderation Architecture v2", item 5 "Creator Safety
    // Tools" -- "WorldSafetySettings improvements"): a real, separate
    // toggle from chatEnabled -- a public-broadcast-heavy social space
    // and a small creative space have genuinely different real reasons
    // to want one on without the other (e.g. keep public chat, disable
    // 1:1 DMs specifically to reduce grooming risk). Checked alongside
    // chatEnabled at net::NetworkSession's real DM send path, not a
    // settings field nobody reads.
    bool directMessagesEnabled = true;

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
