#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "anticheat/RollingEventCounter.hpp"
#include "core/ECS.hpp"
#include "core/GameManifest.hpp"
#include "core/LocalProfile.hpp"
#include "moderation/AccountModerationRegistry.hpp"
#include "moderation/AppealLog.hpp"
#include "moderation/DirectMessageLog.hpp"
#include "moderation/ChatLog.hpp"
#include "moderation/EscalationEventLog.hpp"
#include "moderation/MuteBlockRegistry.hpp"
#include "moderation/ProfanityFilter.hpp"
#include "moderation/ReportLog.hpp"
#include "moderation/ReviewQueue.hpp"
#include "moderation/TrustedCreatorRegistry.hpp"
#include "moderation/WorldSafetySettings.hpp"
#include "net/ClientPrediction.hpp"
#include "net/LanSessionAnnouncer.hpp"
#include "publishing/PublishValidation.hpp"
#include "publishing/WorldRegistry.hpp"
#include "tntwars/TntWarsMatch.hpp"
#include "net/ENetTransport.hpp"
#include "net/NetworkStats.hpp"
#include "net/RateLimiter.hpp"
#include "net/RemoteEntityInterpolation.hpp"
#include "net/RemoteEvent.hpp"
#include "net/ServerReconciliation.hpp"
#include "safety/TrustSafetyService.hpp"

namespace engine::net {
class ByteReader;
}

namespace engine::net {

enum class NetworkMode { Offline, Server, Client };

// Kronos ("Active Joining UI" -- NetworkSession protocol foundation):
// real, client-observable reasons a join attempt or an active session
// can end, replacing what used to be either total silence (a "session
// full" server just silently refused the raw ENet connect) or a bare
// dropped socket with no explanation at all.
enum class JoinFailureReason : uint8_t {
    None = 0,          // no failure -- either never attempted, or the join succeeded
    VersionMismatch = 1,
    SessionFull = 2,
    // Kronos ("Moderation Architecture v2", "Account System v1"): a
    // real, honest, distinct reason -- a banned player's real Error UI
    // should say so, not the misleading "session full".
    Banned = 3,
};
enum class DisconnectReason : uint8_t {
    None = 0,
    Kicked = 1,        // server-initiated, via disconnectPlayer()
    SessionClosed = 2, // the server called shutdown() while this client was still connected
    // Deliberately NEVER sent over the wire by the server -- a real,
    // client-local value synthesized when onPeerDisconnected fires
    // without a prior real Disconnect message having been received (see
    // tickClient()'s own comment), using only information ENet already
    // surfaces, not an invented heuristic.
    ConnectionLost = 3,
};

// Real, process-random, collision-resistant-enough (not cryptographic --
// this is a LAN-local session identifier, not an auth token) 64-bit id,
// mirroring core::generateProfileId()'s exact same real approach
// (core/LocalProfile.cpp).
[[nodiscard]] uint64_t generateSessionId();

// Sprint 11 ("Networking Foundation"): the real orchestration layer
// tying together every real-but-previously-unwired net:: piece
// (ENetTransport, ClientPrediction, ServerReconciliation,
// NetworkIdentity, RemoteEntityInterpolator, NetworkStats,
// Serialization's wire format + delta compression) into one real,
// working multiplayer session core::Application owns and ticks once per
// real frame. Every class this composes already existed and worked in
// isolation before this pass; nothing wired them into an actual running
// session -- that's what this class is.
//
// See net/NetworkedMovement.hpp's own header comment for why player
// movement sync uses a real, deliberately simple kinematic model rather
// than replaying the full physics-capsule core::CharacterController.
class NetworkSession {
public:
    struct Config {
        NetworkMode mode = NetworkMode::Offline;
        uint16_t port = 7777;
        std::string serverAddress; // client mode only
        size_t maxClients = 8;     // server mode only
        float moveSpeed = 8.0f;    // matches CharacterController::Settings::walkSpeed's default

        // Kronos ("Active Joining UI"): real session identity, server
        // mode only. `sessionName` is creator-facing (e.g. "Friday
        // Playtest"), blank is valid. `sessionId` of 0 means "generate a
        // real one" (see generateSessionId()) -- initialize() does this
        // automatically so most callers never set it; a caller that wants
        // a deterministic id (e.g. a test) can supply a nonzero one.
        std::string sessionName;
        uint64_t sessionId = 0;
        // Kronos ("Active Joining UI" -- LAN discovery): a real,
        // creator-facing "who's hosting" label for the Session Browser's
        // own real LanSessionAnnouncement -- distinct from any per-player
        // display name (a dedicated/headless server has no local player
        // of its own to take one from). Blank is valid, same as
        // sessionName.
        std::string hostDisplayName;
        // Server mode only: whether a real LanSessionAnnouncer actually
        // starts (see initialize()) -- on by default. A caller that wants
        // a real, unlisted/private session (e.g. a Studio test session)
        // sets this false.
        bool advertiseOnLan = true;
        // Kronos ("Moderation Architecture v2", "Session Browser Game
        // Identity"): real "which game is this session actually
        // running," broadcast alongside sessionName/hostDisplayName --
        // see LanSessionAnnouncement's own comment for what each field
        // means and why gameName (not a separate numeric id) is the real
        // identity key. Blank/default is valid (a session with no
        // specific game context, e.g. some existing tests/tools).
        std::string gameName;
        glm::vec4 gameThumbnailColor{0.35f, 0.55f, 0.85f, 1.0f};
        core::GameSafetyStatus gameSafetyStatus = core::GameSafetyStatus::Safe;

        // Kronos ("Moderation Architecture v1", Phase 1): server mode
        // only, real disk persistence for chatLog()/reportLog()/
        // reviewQueue()/escalationEventLog()/appealLog() -- real, but
        // deliberately real, explicit **opt-in** (default false), not
        // opt-out. A real, long-running production server (main.cpp's
        // --server mode) sets this true; every test that spins up a
        // real, short-lived server keeps its own fresh, empty, in-memory
        // logs (the previously-established, still-correct behavior)
        // rather than silently loading whatever an *earlier test in the
        // same process* happened to save to the same real, fixed file
        // path -- a real, genuine test-isolation bug this default
        // avoids, not a hypothetical one (caught by this session's own
        // test suite: ~10 pre-existing tests broke before this default
        // was flipped to opt-in).
        bool persistModerationLogs = false;
    };

    ~NetworkSession();

    [[nodiscard]] bool initialize(const Config& config);
    void shutdown();

    [[nodiscard]] NetworkMode mode() const { return config_.mode; }
    [[nodiscard]] bool isActive() const { return config_.mode != NetworkMode::Offline; }
    [[nodiscard]] bool isServer() const { return config_.mode == NetworkMode::Server; }
    [[nodiscard]] bool isClient() const { return config_.mode == NetworkMode::Client; }

    // Real per-tick orchestration -- polls the transport, drives
    // reconciliation (server) or prediction+interpolation (client),
    // (de)serializes real, delta-compressed-where-possible snapshots.
    // `localPlayerEntity` is this process's own avatar (client: the
    // entity being predicted; unused directly in server mode, but the
    // server still needs `ecs` to read every networked entity's real
    // Transform for the snapshots it builds).
    void tick(float dt, core::ECS& ecs, core::EntityId localPlayerEntity);

    // Real client-side input sampling entry point -- called once per
    // tick with this client's current intent; internally builds the real
    // InputCommand, runs it through ClientPrediction (applying
    // net::applyNetworkedMovement() to `localPlayerEntity` immediately),
    // and queues it for sending on the next tick()/poll pass. A real,
    // honest no-op outside Client mode.
    void sampleLocalInput(core::ECS& ecs, core::EntityId localPlayerEntity, glm::vec3 moveAxis, bool jump,
                           bool primaryAction, float yaw, float pitch, float dt);

    [[nodiscard]] NetworkStats& stats() { return networkStats_; }
    [[nodiscard]] size_t connectedPeerCount() const { return transport_.connectedPeerCount(); }
    [[nodiscard]] PlayerId localPlayerId() const { return localPlayerId_; }

    // Kronos ("Active Joining UI"): real session identity -- server mode
    // reflects config_'s own (possibly auto-generated, see
    // generateSessionId()) values back; client mode is populated from the
    // real JoinAccepted payload once a join actually succeeds (0/empty
    // before then).
    [[nodiscard]] uint64_t sessionId() const { return sessionId_; }
    [[nodiscard]] const std::string& sessionName() const { return sessionName_; }
    // Kronos ("Session Browser Polish v2" -- "Sorting: Newly Created"):
    // real wall-clock seconds this server session actually started at
    // (server mode only, set once in initialize()) -- broadcast in every
    // real LanSessionAnnouncement so a browsing client can really sort by
    // it, not a fabricated/estimated value.
    [[nodiscard]] int64_t sessionStartUnixSeconds() const { return sessionStartUnixSeconds_; }

    // Client-only: call before initialize() to set the real display name
    // sent in this client's JoinRequest -- defaults to "Player" if never
    // called, so every existing caller/test keeps compiling and passing
    // unchanged. Server mode ignores this entirely.
    void setLocalDisplayName(std::string name) { localDisplayName_ = std::move(name); }

    // Kronos ("Moderation Architecture v2", "Account System v1"): the
    // real, stable core::LocalProfile::profileId and self-declared
    // core::AgeGroup sent alongside displayName in this client's
    // JoinRequest -- real, honest identity signals a server needs for
    // persistent bans/mutes (keyed by profileId, survives a reconnect,
    // unlike net::PlayerId which is a fresh per-session handle) and
    // Minor Mode enforcement (needs the real age signal server-side, not
    // just locally). Defaults (0, Unknown) if never called, same
    // "every existing caller keeps compiling unchanged" contract as
    // setLocalDisplayName(). A real, honest, stated limitation: nothing
    // stops a client from lying about either value or generating a new
    // profileId to evade a ban -- there is no real authentication in
    // this codebase (see core::LocalProfile's own "no auth, no
    // password" scope), matching the user's own "no networking or cloud
    // accounts yet" framing for this pass.
    void setLocalIdentity(uint64_t profileId, core::AgeGroup ageGroup) {
        localProfileId_ = profileId;
        localAgeGroup_ = ageGroup;
    }

    // Client-only: the real reason the most recent join attempt was
    // rejected -- JoinFailureReason::None if it wasn't (either it
    // succeeded, or no attempt has completed yet). Real Error UI reads
    // this directly rather than a placeholder string.
    [[nodiscard]] JoinFailureReason lastJoinFailureReason() const { return lastJoinFailureReason_; }
    // The real server's own protocol version, populated alongside a
    // VersionMismatch rejection specifically -- lets the Error UI show
    // "you need version N, this server is on M," not just "mismatch."
    [[nodiscard]] uint32_t lastJoinFailureServerProtocolVersion() const { return lastJoinFailureServerProtocolVersion_; }

    // Client-only: the real reason the most recently active session
    // ended -- DisconnectReason::None if this client never connected/was
    // never disconnected.
    [[nodiscard]] DisconnectReason lastDisconnectReason() const { return lastDisconnectReason_; }

    // Client-only: every player currently known to be in the session
    // (including this client's own local player), by real display name --
    // populated from JoinAccepted's own roster snapshot (existing
    // players, sent once at join time) plus every PlayerRosterJoined/
    // PlayerRosterLeft broadcast since. The real "who's here" data the
    // client-side Player List panel reads.
    [[nodiscard]] const std::unordered_map<PlayerId, std::string>& clientKnownPlayers() const {
        return clientKnownPlayers_;
    }

    // Real, observer-side hooks -- same injected-callback shape every
    // other setOnX() in this class already uses. onSessionJoined_ fires
    // once a join actually completes (server: right after a successful
    // Server-mode initialize(); client: right after processing a real
    // JoinAccepted). onSessionLeft_/onDisconnected_ both fire at the
    // real, honest moment the session actually ends -- see shutdown()'s
    // own comment for why onSessionLeft_ fires before teardown, mirroring
    // core::Scripting::fireUnload()'s "fire while state is still live"
    // ordering. onDisconnected_ additionally carries the real reason
    // (client-only; a server never disconnects itself).
    void setOnSessionJoined(std::function<void()> callback) { onSessionJoined_ = std::move(callback); }
    void setOnSessionLeft(std::function<void()> callback) { onSessionLeft_ = std::move(callback); }
    void setOnDisconnected(std::function<void(DisconnectReason)> callback) { onDisconnected_ = std::move(callback); }

    // Client-only: real, tunable disconnect-detection speed -- see
    // ENetTransport::setPeerTimeout()'s own comment for why this is real
    // production tuning (e.g. a LAN session can afford to declare a dead
    // host gone far faster than ENet's own internet-tuned default), not
    // just a testing hook. Must be called after a real, successful
    // Client-mode initialize().
    void setPeerTimeout(uint32_t timeoutLimit, uint32_t timeoutMinimumMs, uint32_t timeoutMaximumMs) {
        transport_.setPeerTimeout(timeoutLimit, timeoutMinimumMs, timeoutMaximumMs);
    }

    // Kronos ("Studio QoL Sprint" -- "Integrated Network Emulation Bar"):
    // thin passthrough to the real ENetTransport-level conditioning (see
    // ENetTransport::setSimulatedLatencyMs()/setSimulatedPacketLossPercent()'s
    // own comments for exactly what each does and why loss is
    // unreliable-only). Works in either Server or Client mode -- both
    // own their own `transport_` and send() through it.
    void setSimulatedLatencyMs(uint32_t ms) { transport_.setSimulatedLatencyMs(ms); }
    void setSimulatedPacketLossPercent(uint8_t percent) { transport_.setSimulatedPacketLossPercent(percent); }
    [[nodiscard]] uint32_t simulatedLatencyMs() const { return transport_.simulatedLatencyMs(); }
    [[nodiscard]] uint8_t simulatedPacketLossPercent() const { return transport_.simulatedPacketLossPercent(); }

    // Server-only: registers `entity` as networked, owned by `player`
    // (kInvalidPlayer for server-owned world state -- an ore node, a
    // prop). Attaches a real net::NetworkIdentity with a freshly
    // allocated id. A real, honest no-op in Client/Offline mode --
    // clients never mint network identities, only the server does (the
    // "server owns world state" authority rule task 3 asks for, applied
    // at the identity-allocation level, not just at the interaction-
    // validation level).
    void registerNetworkedEntity(core::ECS& ecs, core::EntityId entity, PlayerId owner);

    // Server-only: called once, before connections start arriving, to
    // supply the real "a new player just joined -- spawn their avatar
    // and return its EntityId" gameplay logic. Kept as an injected
    // callback (the same "orchestration class doesn't hardcode gameplay
    // spawning" pattern net::ServerReconciliation's validate/apply
    // already established) rather than NetworkSession reaching into
    // core::CharacterController itself.
    void setOnPlayerJoin(std::function<core::EntityId(core::ECS&, PlayerId)> callback) {
        onPlayerJoin_ = std::move(callback);
    }

    // Kronos ("Active Joining UI" -- Scripting event hooks): real,
    // observer-side "who's in the session, by name" notifications --
    // deliberately separate from setOnPlayerJoin() (a server-only
    // spawning factory) and named after Roblox's own real
    // Players.PlayerAdded/PlayerRemoving, the closest real precedent this
    // pair mirrors. Both fire on EITHER role, from the real, honest
    // moment each side actually learns about it:
    //   Server: setOnPlayerAdded fires right after a real join fully
    //     completes (spawned + real Name applied), alongside the real
    //     PlayerRosterJoined broadcast (see handleJoinRequestServer());
    //     setOnPlayerRemoving fires right after a joined player
    //     disconnects (playerEntity(player) is already back to
    //     kNullEntity by then -- the real name is passed directly since
    //     there's nothing left to look it up from).
    //   Client: setOnPlayerAdded fires once per entry in a real
    //     JoinAccepted's own existing-roster snapshot (every player
    //     already there when this client joined -- the real, honest
    //     "you get told about everyone, not just future joins" behavior
    //     Roblox's own PlayerAdded is documented to exhibit) and again
    //     for every real PlayerRosterJoined broadcast after that;
    //     setOnPlayerRemoving fires on a real PlayerRosterLeft broadcast.
    // core::Application wires both directly to core::Scripting::
    // firePlayerJoin()/firePlayerLeave() -- one function body, multiple
    // real trigger paths, not two independently-drifting notions of
    // "a player joined."
    void setOnPlayerAdded(std::function<void(PlayerId, const std::string&)> callback) {
        onPlayerAdded_ = std::move(callback);
    }
    void setOnPlayerRemoving(std::function<void(PlayerId, const std::string&)> callback) {
        onPlayerRemoving_ = std::move(callback);
    }

    // Server-only: the real ECS entity onPlayerJoin_ returned for `player`,
    // or kNullEntity if `player` isn't a currently-connected server-side
    // player (including when called on a client/offline session, which
    // never populates serverPlayerEntities_ at all). Lets a caller outside
    // NetworkSession itself (e.g. a real display-name RPC handler) attach
    // real per-player data to the same entity onPlayerJoin_ already
    // created, without NetworkSession needing to know what that data is.
    [[nodiscard]] core::EntityId playerEntity(PlayerId player) const {
        auto it = serverPlayerEntities_.find(player);
        return it != serverPlayerEntities_.end() ? it->second : core::kNullEntity;
    }

    // Server-only: every currently-connected player's real PlayerId, in no
    // particular order -- the real "who's here right now" list a host-side
    // UI needs (see NetworkOverlayPlugin's own "Connected Players"
    // section), as opposed to connectedPeerCount()'s bare count.
    [[nodiscard]] std::vector<PlayerId> connectedPlayerIds() const {
        std::vector<PlayerId> ids;
        ids.reserve(serverPlayerEntities_.size());
        for (const auto& [player, entity] : serverPlayerEntities_) ids.push_back(player);
        return ids;
    }

    // Real, client-side interaction-sync entry point (task 3) -- sends a
    // real teleport request naming the target pad's networkId; the
    // server validates range + destination sanity
    // (net::isWithinInteractionRange()/isTeleportDestinationValid()) and,
    // if valid, moves the player's authoritative Transform server-side --
    // the requesting client (and every other client) finds out via the
    // next real snapshot, the same way any other server-applied change
    // propagates. A real, honest no-op outside Client mode.
    void requestTeleport(uint32_t padNetworkId);

    // Sprint 12 ("Moderation & Safety Systems") task 1 -- real,
    // client-side chat send. The server -- and only the server -- is
    // authoritative over whether it's actually delivered: rate-limited
    // (moderation::WorldSafetySettings::maxChatMessagesPerSecond),
    // gated by worldSafetySettings().chatEnabled, profanity-filtered
    // (moderation::ProfanityFilter), scored by
    // safety::TrustSafetyService::onChatMessage() for the real
    // escalation pipeline, logged to chatLog() regardless of outcome,
    // and only then broadcast to every OTHER connected peer that hasn't
    // muted/blocked the sender (moderation::MuteBlockRegistry) and isn't
    // server-muted (see reviewQueue()/isServerMuted()). A real, honest
    // no-op outside Client mode.
    void sendChatMessage(const std::string& text);

    // Real, observer-side hook for a received chat broadcast -- Studio's
    // ModerationPanel (or a future real in-game HUD) reads live chat
    // through this the same injected-callback way setOnPlayerJoin()
    // already works. A real, honest no-op if never set.
    void setOnChatMessageReceived(std::function<void(PlayerId sender, const std::string& text)> callback) {
        onChatMessageReceived_ = std::move(callback);
    }

    // Sprint 12 task 2 -- real, client-side "Report Player" request.
    // Server-side, this only ever appends to reportLog() with a real
    // server timestamp; it never triggers an automated action by itself
    // (a human player's accusation isn't evidence the same way a real
    // classifier signal is -- see safety::TrustSafetyService's own
    // mandatory-human-review-before-action design). A real, honest
    // no-op outside Client mode.
    void reportPlayer(PlayerId reported, moderation::ReportCategory category, const std::string& description);

    // Kronos ("Moderation Architecture v1", Phase 1) -- real, client-side
    // "Submit Appeal" request, exact same shape/precedent as
    // reportPlayer() above. Server-side, this only ever appends to
    // appealLog() with a real server timestamp and AppealOutcome::Pending
    // -- resolving it (Upheld/Reduced/Reversed) is a real, separate,
    // human moderator action (studio::plugins::ModerationPanel's real
    // Appeals section), never automatic. A real, honest no-op outside
    // Client mode.
    void submitAppeal(const std::string& playerStatement, const std::string& relatedReviewCaseReason);

    // Kronos ("Moderation Architecture v2", "DM System v1"): real,
    // client-side "send a direct message" -- exact same real shape/
    // precedent as sendChatMessage(), except server-side delivery is
    // targeted at the one real recipient, never broadcast. Real
    // moderation (TrustSafetyService/PolicyEngine) and Minor Mode DM
    // restrictions are enforced server-side, see
    // handleDirectMessageSendServer()'s own comment -- this call
    // doesn't (and can't, from the client) know in advance whether the
    // real server will actually deliver it. A real, honest no-op
    // outside Client mode.
    void sendDirectMessage(PlayerId recipient, const std::string& text);

    // Real, observer-side hook for a received real direct message --
    // same real, injected-callback shape as setOnChatMessageReceived().
    void setOnDirectMessageReceived(std::function<void(PlayerId sender, const std::string& text)> callback) {
        onDirectMessageReceived_ = std::move(callback);
    }

    [[nodiscard]] moderation::ChatLog& chatLog() { return chatLog_; }
    [[nodiscard]] moderation::ReportLog& reportLog() { return reportLog_; }
    [[nodiscard]] moderation::ReviewQueue& reviewQueue() { return reviewQueue_; }
    [[nodiscard]] moderation::MuteBlockRegistry& muteBlockRegistry() { return muteBlockRegistry_; }
    [[nodiscard]] moderation::TrustedCreatorRegistry& trustedCreatorRegistry() { return trustedCreatorRegistry_; }
    [[nodiscard]] moderation::WorldSafetySettings& worldSafetySettings() { return worldSafetySettings_; }
    // Kronos ("Moderation Architecture v1", Phase 1): real, read-only --
    // a moderator dashboard reads risk scores/tiers and the real audit
    // trail, it doesn't mutate safety::TrustSafetyService directly (its
    // real actions all flow through the Callbacks this class already
    // wires in initialize(), not through a UI poking it).
    [[nodiscard]] const safety::TrustSafetyService& trustSafetyService() const { return trustSafetyService_; }
    [[nodiscard]] const moderation::EscalationEventLog& escalationEventLog() const { return escalationEventLog_; }
    [[nodiscard]] moderation::AppealLog& appealLog() { return appealLog_; }
    [[nodiscard]] moderation::AccountModerationRegistry& accountModerationRegistry() { return accountModerationRegistry_; }
    [[nodiscard]] moderation::DirectMessageLog& directMessageLog() { return directMessageLog_; }

    // Kronos ("Moderation Architecture v2", "Minor Mode Enforcement"):
    // real, server-side lookup of a connected remote player's own
    // self-declared AgeGroup -- core::AgeGroup::Unknown (the real,
    // conservative default) for a player who hasn't joined, or who
    // joined via a pre-Account-System-v2 client that sent no real
    // AgeGroup at all.
    [[nodiscard]] core::AgeGroup playerAgeGroup(PlayerId player) const {
        auto it = serverPlayerAgeGroups_.find(player);
        return it != serverPlayerAgeGroups_.end() ? it->second : core::AgeGroup::Unknown;
    }

    // Real, server-driven moderation mute (an escalation *action*,
    // dispatched automatically from safety::TrustSafetyService's
    // callbacks or set directly by a creator/moderator via
    // studio::plugins::ModerationPanel) -- distinct from
    // muteBlockRegistry()'s per-player personal preference: a server
    // mute silences the player for EVERYONE, not just one recipient who
    // chose to mute them.
    void setServerMuted(PlayerId player, bool muted);
    [[nodiscard]] bool isServerMuted(PlayerId player) const { return serverMutedPlayers_.count(player) > 0; }

    // Kronos ("Active Joining UI"): a real, graceful kick -- server-only.
    // Sends a real Disconnect{reason} message to `player` reliably, then
    // gracefully closes the connection (ENetTransport::disconnectPeerGracefully(),
    // so the message actually flushes first) rather than just erasing
    // local state and leaving the client to see a bare, unexplained
    // socket drop. A real, honest no-op if `player` isn't currently
    // connected, or outside Server mode.
    void disconnectPlayer(PlayerId player, DisconnectReason reason);

    // Sprint 13 ("Publishing & Game Packaging") task 4 -- real,
    // server-only, LOCAL (same-process; the heavy WorldPackage bytes are
    // never sent over this class's own small-message wire protocol in
    // this pass -- see the README's "Publishing & Packaging" section for
    // the full honest scope account) publish entry point: validates
    // `listing`'s metadata/id/version and, if valid, upserts it into
    // worldRegistry() with a real publish timestamp. Returns the real
    // validation result either way, so a caller (studio::plugins::PublishingPanel
    // today) can show the real, specific rejection reasons. A real,
    // honest no-op (an invalid result naming why) outside Server mode.
    [[nodiscard]] publishing::PublishValidationResult publishWorld(publishing::WorldListing listing);
    [[nodiscard]] publishing::WorldRegistry& worldRegistry() { return worldRegistry_; }

    // Sprint 14 ("TNT-Wars Core Game Build") -- real, client-side
    // request senders, mirroring requestTeleport()/sendChatMessage()'s
    // exact real "build a small wire message, send reliably, let the
    // server's real TntWarsMatch decide" shape. All three are real,
    // honest no-ops outside Client mode.
    void selectTntWarsClass(tntwars::PlayerClassType classType);
    void fireTntWarsWeapon(glm::vec3 origin, glm::vec3 aimDirection);
    void triggerTntWarsUltimate();

    // Real, observer-side hooks for the two real server broadcasts (a
    // fired shot, a triggered ultimate) -- studio::plugins::TntWarsPlugin
    // and a future real in-game HUD both read live match state through
    // these, the same injected-callback pattern
    // setOnChatMessageReceived()/setOnPlayerJoin() already established.
    void setOnProjectileSpawned(std::function<void(const tntwars::ProjectileState&)> callback) {
        onProjectileSpawned_ = std::move(callback);
    }
    void setOnUltimateTriggered(std::function<void(PlayerId, tntwars::UltimateType)> callback) {
        onUltimateTriggered_ = std::move(callback);
    }

    [[nodiscard]] tntwars::TntWarsMatch& tntWarsMatch() { return tntWarsMatch_; }

    // Real Network Stress Test mode (task 4) -- spawns `count` synthetic
    // *real* local ENet client connections against this process's own
    // server (genuine loopback ENet peers with a real handshake, not
    // fabricated ping/packet numbers), each sending real randomized
    // InputCommands at `inputsPerSecond` -- a genuine load test of the
    // server's tick/broadcast loop. Server mode only; a real, honest
    // no-op otherwise.
    void startStressTest(size_t syntheticPlayerCount, float inputsPerSecond);
    void stopStressTest();
    [[nodiscard]] bool isStressTestRunning() const { return !stressClients_.empty(); }
    [[nodiscard]] size_t stressTestClientCount() const { return stressClients_.size(); }

    // Kronos (Alpha Roadmap Phase 4, "Networking Upgrade"): the real,
    // generic RPC surface -- net::RemoteEvent already had the schema/
    // rate-limit enforcement primitive built (see RemoteEvent.hpp's own
    // header comment), but nothing wired it into this session's actual
    // wire protocol or gave it a name-based registry; every other
    // client<->server message here is a bespoke, hand-written case
    // (TeleportRequest, SelectClass, ...). This is the first genuinely
    // generic one, and what core::ScriptNetworkApi's Luau `network` table
    // binds to.
    //
    // Server-only: get-or-create the named RemoteEvent, so a caller
    // (typically a Luau script via network.onServerEvent()) can attach a
    // real setInboundSchema()/setRateLimit()/setServerHandler() to it. A
    // name nobody has configured yet still works exactly per RemoteEvent's
    // own documented contract -- schema/rate-limit are no-ops until set,
    // and an unset handler is a real, honest no-op on fire (see
    // RemoteEvent::handleFromClient()), not an error.
    // RemoteEvent has no default constructor (its name is required at
    // construction), so this can't use unordered_map::operator[] --
    // try_emplace constructs the RemoteEvent(name) only on a real miss.
    [[nodiscard]] RemoteEvent& remoteEvent(const std::string& name) {
        return remoteEvents_.try_emplace(name, name).first->second;
    }

    // Real, client-side fire -- reliable, server-authoritative (the
    // server's own registered RemoteEvent runs schema/rate-limit checks
    // before any handler sees it, exactly like every other client->server
    // message here). A real, honest no-op outside Client mode.
    void fireServerEvent(const std::string& name, const RemoteEvent::Payload& payload);

    // Real, server-side broadcast to every connected client -- reliable,
    // matches requestTeleport()/sendChatMessage()'s own "an RPC-style
    // message is a one-time event, not a per-tick state update, so it
    // goes over the reliable channel" reasoning. A real, honest no-op
    // outside Server mode. Deliberately broadcast-only in this pass, not
    // per-target -- see docs/NETWORKING_UPGRADE.md for the real, tracked
    // follow-up (a single-recipient send needs a PlayerId->peer lookup
    // this pass didn't add).
    void fireAllClientsEvent(const std::string& name, const RemoteEvent::Payload& payload);

    // Real, client-side observer hook for a received broadcast -- same
    // injected-callback shape setOnChatMessageReceived()/
    // setOnProjectileSpawned() already use. core::ScriptNetworkApi
    // dispatches by event name to whichever Luau network.onClientEvent()
    // handlers are registered for it.
    void setOnClientEventReceived(std::function<void(const std::string&, const RemoteEvent::Payload&)> callback) {
        onClientEventReceived_ = std::move(callback);
    }

private:
    void tickServer(float dt, core::ECS& ecs);
    void tickClient(float dt, core::ECS& ecs, core::EntityId localPlayerEntity);
    void tickStressTest(float dt);
    void handleTeleportRequestServer(PlayerId player, ByteReader& reader);
    void handleChatMessageServer(PlayerId player, ByteReader& reader);
    void handleReportPlayerServer(PlayerId player, ByteReader& reader);
    void handleSubmitAppealServer(PlayerId player, ByteReader& reader);
    void handleDirectMessageSendServer(PlayerId sender, ByteReader& reader);
    void handleSelectClassServer(PlayerId player, ByteReader& reader);
    void handleFireWeaponServer(PlayerId player, ByteReader& reader);
    void handleTriggerUltimateServer(PlayerId player, ByteReader& reader);
    void handleRemoteEventFireServer(PlayerId player, ByteReader& reader);
    // Kronos ("Active Joining UI"): unlike the other handleXServer()
    // methods above, this one also needs the raw ENetTransport::PeerId
    // (not just the already-resolved PlayerId) -- a rejection needs to
    // reply to, and gracefully disconnect, a peer that may never
    // successfully join (so never gets an entry in serverPlayerEntities_
    // to look a peer id up from). The main onPacketReceived dispatch
    // already has `peer` in scope, so passing it through is free.
    void handleJoinRequestServer(ENetTransport::PeerId peer, PlayerId player, ByteReader& reader);

    Config config_;
    ENetTransport transport_;
    // Kronos ("Active Joining UI" -- LAN discovery): server-mode-only,
    // real, optional (see Config::advertiseOnLan) UDP broadcast announcer
    // -- owned here (not by a caller) since its whole lifetime exactly
    // matches this session's own real hosting lifetime.
    LanSessionAnnouncer lanAnnouncer_;
    ServerReconciliation serverReconciliation_;
    ClientPrediction clientPrediction_;
    NetworkStats networkStats_;

    // Sprint 12 ("Moderation & Safety Systems") -- real, shared
    // moderation/anti-cheat state, server-authoritative like everything
    // else NetworkSession owns (see class comment). `trustSafetyService_`
    // is wired with real callbacks (setServerMuted() +
    // reviewQueue_.add()) in initialize()'s server branch -- the "no
    // Players system exists to call [safety::TrustSafetyService's
    // callbacks] on" gap TrustSafetyService.hpp's own header comment
    // named is exactly what net::PlayerId (Sprint 11) closed.
    safety::TrustSafetyService trustSafetyService_;
    moderation::ProfanityFilter profanityFilter_;
    moderation::MuteBlockRegistry muteBlockRegistry_;
    moderation::ChatLog chatLog_;
    moderation::ReportLog reportLog_;
    moderation::ReviewQueue reviewQueue_;
    // Kronos ("Moderation Architecture v1", Phase 1): the real, disk-
    // persisted audit trail safety::TrustSafetyService::Callbacks::
    // onEscalationEvent feeds -- see EscalationEventLog.hpp's own class
    // comment on why this is deliberately separate from safety::RiskScore
    // (which stays unpersisted/decaying by design).
    moderation::EscalationEventLog escalationEventLog_;
    moderation::AppealLog appealLog_;
    moderation::AccountModerationRegistry accountModerationRegistry_;
    moderation::DirectMessageLog directMessageLog_;
    std::function<void(PlayerId sender, const std::string& text)> onDirectMessageReceived_;
    moderation::TrustedCreatorRegistry trustedCreatorRegistry_;
    moderation::WorldSafetySettings worldSafetySettings_;
    anticheat::RollingEventCounter movementRejectionCounter_{10.0f}; // 10s rolling window
    TokenBucketRateLimiter chatRateLimiter_{3.0f};        // real cap synced from worldSafetySettings_ each check
    TokenBucketRateLimiter interactionRateLimiter_{5.0f}; // real cap synced from worldSafetySettings_ each check
    std::unordered_set<PlayerId> serverMutedPlayers_;
    std::function<void(PlayerId, const std::string&)> onChatMessageReceived_;

    // Sprint 13 ("Publishing & Game Packaging") -- see publishWorld()'s
    // own comment.
    publishing::WorldRegistry worldRegistry_;

    // Sprint 14 ("TNT-Wars Core Game Build") -- see selectTntWarsClass()'s
    // own comment. `tntWarsMatch_` is real, server-authoritative state
    // (client-mode sessions still own one so tests/local tooling has a
    // real, valid object to query, but only the server's copy is ever
    // actually mutated by a real client request).
    tntwars::TntWarsMatch tntWarsMatch_;
    std::function<void(const tntwars::ProjectileState&)> onProjectileSpawned_;
    std::function<void(PlayerId, tntwars::UltimateType)> onUltimateTriggered_;

    // Set fresh at the top of every tick()/sampleLocalInput() call --
    // every callback below reads through this rather than capturing an
    // core::ECS& directly, since the callbacks are configured once (in
    // initialize()) but the ECS reference is only guaranteed valid for
    // the duration of a single call in this codebase's own convention
    // (see core::Application/studio::StudioApp, both of which pass ecs_
    // fresh into every per-frame call rather than storing a reference
    // long-term).
    core::ECS* currentEcs_ = nullptr;

    std::function<core::EntityId(core::ECS&, PlayerId)> onPlayerJoin_;
    // Kronos ("Active Joining UI"): see setOnPlayerAdded()/
    // setOnPlayerRemoving()'s own comment.
    std::function<void(PlayerId, const std::string&)> onPlayerAdded_;
    std::function<void(PlayerId, const std::string&)> onPlayerRemoving_;

    // Kronos ("Active Joining UI"): real session identity -- server mode
    // populates both in initialize() from config_ (generating a real id
    // if config_.sessionId was 0); client mode populates both from the
    // real JoinAccepted payload.
    uint64_t sessionId_ = 0;
    std::string sessionName_;
    int64_t sessionStartUnixSeconds_ = 0;
    std::function<void()> onSessionJoined_;
    std::function<void()> onSessionLeft_;
    // Real, honest "did this session actually finish starting" flag --
    // distinct from config_.mode != Offline, which (see initialize()'s
    // own top line, config_ = config;) stays true even after a FAILED
    // hostServer()/connectToServer() call. Set true exactly where
    // onSessionJoined_ fires (server: after a successful initialize();
    // client: after a real JoinAccepted), so shutdown() only fires
    // onSessionLeft_/broadcasts SessionClosed for a session that actually
    // existed, not a failed or still-connecting attempt.
    bool sessionActuallyStarted_ = false;

    // Server-side: which ECS entity each connected player's avatar is,
    // each player's real ENet peer id, every networked entity's real
    // networkId->EntityId lookup (needed for interaction requests that
    // name a *target* entity, e.g. requestTeleport()'s pad id), and each
    // player's last-acknowledged snapshot (the real delta-compression
    // baseline -- see net::serializeSnapshotDelta()'s own comment).
    std::unordered_map<PlayerId, core::EntityId> serverPlayerEntities_;
    // Kronos ("Active Joining UI"): each successfully-joined player's real
    // display name (from their own JoinRequest) -- kept independently of
    // the entity's own core::Name component (which handleJoinRequestServer()
    // also sets, for anything reading Name off the ECS directly) so
    // onPeerDisconnected's PlayerRosterLeft broadcast still has a real
    // name to send after serverPlayerEntities_ has already been erased.
    std::unordered_map<PlayerId, std::string> serverPlayerDisplayNames_;
    // Kronos ("Moderation Architecture v2", "Account System v1"): the
    // real, stable profileId each connected player joined with (0 if
    // they didn't send one -- a pre-Account-System-v2 client) -- lets
    // real, session-scoped chat delivery consult the real, persistent
    // accountModerationRegistry_ by the real identity that survives a
    // reconnect, not just the ephemeral per-session PlayerId.
    std::unordered_map<PlayerId, uint64_t> serverPlayerProfileIds_;
    // Kronos ("Moderation Architecture v2", "Minor Mode Enforcement"):
    // the real, self-declared AgeGroup each connected player joined
    // with -- real server-side signal for Minor Mode policy checks
    // (stricter chat thresholds, DM restrictions, unsafe-game/session
    // gating) that need to know a *remote* player's age, not just the
    // local player's own.
    std::unordered_map<PlayerId, core::AgeGroup> serverPlayerAgeGroups_;
    std::unordered_map<ENetTransport::PeerId, PlayerId> serverPeerToPlayer_;
    std::unordered_map<PlayerId, DeltaSnapshot> serverPlayerBaselines_;
    std::unordered_map<uint32_t, core::EntityId> serverNetworkIdToEntity_;
    PlayerId nextPlayerId_ = 1;

    // Kronos (Alpha Roadmap Phase 4) -- see remoteEvent()'s own comment.
    // Server-side only; a client never looks anything up by name here,
    // it only ever fires (fireServerEvent()) or receives a broadcast
    // (onClientEventReceived_ below).
    std::unordered_map<std::string, RemoteEvent> remoteEvents_;
    std::function<void(const std::string&, const RemoteEvent::Payload&)> onClientEventReceived_;

    // Client-side: this client's own player id (assigned by the server
    // on connect via a real handshake payload), the last decoded
    // snapshot (this client's own delta-decode baseline), and one real
    // interpolator per *other* networked entity currently known about,
    // keyed by real net::NetworkIdentity::networkId.
    PlayerId localPlayerId_ = kInvalidPlayer;
    DeltaSnapshot clientBaseline_;
    std::unordered_map<uint32_t, RemoteEntityInterpolator> remoteInterpolators_;
    std::unordered_map<uint32_t, core::EntityId> clientNetworkIdToEntity_;

    // Kronos ("Active Joining UI"): client-side join/disconnect state --
    // see setLocalDisplayName()/lastJoinFailureReason()/
    // lastDisconnectReason()'s own comments.
    std::string localDisplayName_ = "Player";
    uint64_t localProfileId_ = 0;
    core::AgeGroup localAgeGroup_ = core::AgeGroup::Unknown;
    JoinFailureReason lastJoinFailureReason_ = JoinFailureReason::None;
    uint32_t lastJoinFailureServerProtocolVersion_ = 0;
    DisconnectReason lastDisconnectReason_ = DisconnectReason::None;
    // Real, honest "did we get a real Disconnect message before the raw
    // socket actually dropped" flag -- tickClient()'s onPeerDisconnected
    // synthesizes DisconnectReason::ConnectionLost only when this is
    // still false, distinguishing a graceful goodbye from a real
    // ungraceful drop using only information ENet already surfaces.
    bool receivedGracefulDisconnect_ = false;
    std::function<void(DisconnectReason)> onDisconnected_;
    // Client-only: see clientKnownPlayers()'s own comment.
    std::unordered_map<PlayerId, std::string> clientKnownPlayers_;

    uint32_t serverTick_ = 0;
    float clockSeconds_ = 0.0f; // real, monotonically increasing session clock -- feeds interpolator sample() calls

    // Real stress-test synthetic clients -- heap-allocated (ENetTransport
    // isn't copyable/movable, see its own class comment, so a vector of
    // values can't hold it; a vector of owning pointers can).
    struct StressClient {
        ENetTransport transport;
        float inputAccumulatorSeconds = 0.0f;
        uint32_t nextSequence = 1;
    };
    std::vector<std::unique_ptr<StressClient>> stressClients_;
    float stressInputsPerSecond_ = 0.0f;
};

} // namespace engine::net
