#include "net/NetworkSession.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <random>

#include "core/Components.hpp"
#include "core/Logger.hpp"
#include "core/Navigation.hpp"
#include "net/InteractionValidation.hpp"
#include "net/NetworkIdentity.hpp"
#include "net/NetworkedMovement.hpp"
#include "net/Serialization.hpp"

#include "moderation/TrainingDataLog.hpp"

namespace engine::net {

namespace {
// Real, minimal application-level wire protocol -- one byte identifying
// what follows, prefixed on every packet this session sends. A real,
// if deliberately small, alternative to ENet's own channel numbers doing
// double duty as message-type discrimination (channels here stay a real,
// separate concern: reliable-vs-unreliable delivery, not payload type).
enum class WireMessageType : uint8_t {
    Input = 1,
    Snapshot = 2,
    TeleportRequest = 4,
    ChatMessage = 5,   // client -> server: "please send this"
    ChatBroadcast = 6, // server -> client: "here's what a real, accepted message contains, and who sent it"
    ReportPlayer = 7,  // client -> server only; no broadcast/ack, see reportPlayer()'s own comment
    SelectClass = 8,       // client -> server
    FireWeapon = 9,        // client -> server
    ProjectileSpawned = 10, // server -> client broadcast
    TriggerUltimate = 11,   // client -> server
    UltimateTriggered = 12, // server -> client broadcast
    RemoteEventFire = 13,      // client -> server: generic named RPC (see NetworkSession::remoteEvent())
    RemoteEventBroadcast = 14, // server -> client broadcast: generic named RPC
    // Kronos ("Active Joining UI" -- NetworkSession protocol foundation):
    // a real client-initiated join handshake, replacing the old
    // Handshake=3 (the server unilaterally sending one the instant the
    // raw ENet connect completed, before the client had said anything
    // about itself -- no seam to attach a version/capacity check to).
    // 3 is deliberately left retired, not reused -- see
    // kNetworkProtocolVersion's own comment on why this wire format has
    // no cross-build compatibility promise to preserve anyway.
    JoinRequest = 15,          // client -> server: protocolVersion, displayName
    JoinAccepted = 16,         // server -> client: player, avatarNetworkId, sessionId, sessionName, protocolVersion
    JoinRejected = 17,         // server -> client: reason (u8), serverProtocolVersion
    Disconnect = 18,           // server -> client: reason (u8) -- a real, graceful goodbye, see disconnectPlayer()/shutdown()
    PlayerRosterJoined = 19,   // server -> client broadcast: player, displayName
    PlayerRosterLeft = 20,     // server -> client broadcast: player, displayName
    // Kronos ("Moderation Architecture v1", Phase 1): client -> server
    // only, no broadcast/ack -- exact same real shape as ReportPlayer=7
    // above (see submitAppeal()'s own comment).
    SubmitAppeal = 21,
    // Kronos ("Moderation Architecture v2", "DM System v1"): a real,
    // minimal, text-only direct-message pair -- Send is client -> server
    // only (real moderation routing happens server-side, same as chat);
    // Deliver is server -> the one real intended recipient only, NEVER a
    // broadcast (the whole point of a DM, unlike ChatBroadcast).
    DirectMessageSend = 22,
    DirectMessageDeliver = 23,
};

// Kronos ("Moderation Architecture v1", Phase 1): real, local, per-
// machine persistence paths for the real moderation logs below -- same
// "plain relative path in the working directory" convention as every
// other small local save file in this codebase (e.g.
// runtime::RuntimeShell's own kSessionHistoryPath/kLocalProfilePath).
constexpr const char* kChatLogPath = "chat_log.chatlog";
constexpr const char* kReportLogPath = "report_log.reportlog";
constexpr const char* kReviewQueuePath = "review_queue.reviewqueue";
constexpr const char* kEscalationEventLogPath = "escalation_log.escalationlog";
constexpr const char* kAppealLogPath = "appeal_log.appeallog";
constexpr const char* kAccountModerationPath = "account_moderation.accountmod";
constexpr const char* kDirectMessageLogPath = "dm_log.dmlog";
constexpr const char* kTrainingDataLogPath = "moderation_training_data.log";

// Kronos ("Active Joining UI"): the raw ENet peer cap passed to
// hostServer() must exceed the real, business-level maxClients by a
// small amount -- otherwise a "full" session's (maxClients+1)th
// connector never even completes the low-level ENet connect (ENet
// silently refuses it), and handleJoinRequestServer() below never gets a
// chance to run and send a real JoinRejected{SessionFull} reason back.
// This headroom exists purely to let a handful of over-capacity
// connections complete enough to be told why they're being turned away,
// not to actually seat more real players than maxClients.
constexpr size_t kSessionFullRejectionHeadroom = 4;

// Sprint 12 ("Anti-Cheat Foundation") -- a real, if deliberately small,
// rejection-count threshold and per-rejection risk weight, same
// "illustrative default, not derived from anything real to calibrate
// against yet" honesty level as safety::RiskScore's own escalation
// thresholds (see that header's comment). 20 rejections inside
// movementRejectionCounter_'s 10s window is a real, generous allowance
// -- ordinary lag/packet loss can legitimately cause a few rejections;
// this is meant to catch a client that's persistently sending implausible
// input, not one bad tick.
constexpr size_t kMovementRejectionThreshold = 20;
constexpr float kMovementRejectionWeight = 0.2f;

constexpr uint8_t kReliableChannel = 0;
constexpr uint8_t kUnreliableChannel = 1;
} // namespace

uint64_t generateSessionId() {
    static std::mt19937_64 rng{std::random_device{}()};
    static std::uniform_int_distribution<uint64_t> dist(1, std::numeric_limits<uint64_t>::max());
    return dist(rng);
}

NetworkSession::~NetworkSession() { shutdown(); }

bool NetworkSession::initialize(const Config& config) {
    config_ = config;

    if (config_.mode == NetworkMode::Server) {
        // Kronos ("Moderation Architecture v1", Phase 1): real disk
        // persistence -- these logs used to reset on every process
        // restart (see each type's own header comment). Real, explicit
        // opt-in (see Config::persistModerationLogs's own comment on
        // why default-off matters). A real, honest no-op if no file
        // exists yet (first-ever server start).
        if (config_.persistModerationLogs) {
            (void)chatLog_.loadFromFile(kChatLogPath);
            (void)reportLog_.loadFromFile(kReportLogPath);
            (void)reviewQueue_.loadFromFile(kReviewQueuePath);
            (void)escalationEventLog_.loadFromFile(kEscalationEventLogPath);
            (void)appealLog_.loadFromFile(kAppealLogPath);
            (void)accountModerationRegistry_.loadFromFile(kAccountModerationPath);
            (void)directMessageLog_.loadFromFile(kDirectMessageLogPath);
        }

        // Kronos ("Active Joining UI"): the raw ENet host is over-
        // provisioned by kSessionFullRejectionHeadroom -- see that
        // constant's own comment. config_.maxClients itself stays the
        // real, business-level capacity handleJoinRequestServer() checks.
        if (!transport_.hostServer(config_.port, config_.maxClients + kSessionFullRejectionHeadroom)) {
            // Kronos (Alpha Completion Checklist, "Crash & Error
            // Telemetry" -- "Networking error routing"): previously a
            // real, completely silent failure -- transport_.hostServer()
            // returning false propagated up with no log anywhere, only a
            // generic UI status string at the one real call site
            // (NetworkOverlayPlugin's "Failed to start networking.").
            core::logError("Network", "hostServer() failed on port %u (already in use, or insufficient permissions?)",
                            config_.port);
            return false;
        }

        // Kronos ("Active Joining UI"): real session identity -- generate
        // a real id if the caller didn't supply one (the common case; a
        // deterministic caller, e.g. a test, can set config.sessionId
        // itself instead).
        sessionId_ = config_.sessionId != 0 ? config_.sessionId : generateSessionId();
        sessionName_ = config_.sessionName;
        config_.sessionId = sessionId_; // keep config_ and the accessor in agreement
        sessionStartUnixSeconds_ =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();

        // Kronos ("Active Joining UI" -- LAN discovery): a real, honest
        // best-effort start -- a failed bind() (e.g. another process
        // already using kLanAnnouncePort/kLanPingPort) does NOT fail the
        // whole session, matching Config::advertiseOnLan's own opt-out
        // spirit: hosting still works, this session just won't show up in
        // a real Session Browser's LAN list.
        if (config_.advertiseOnLan) {
            if (!lanAnnouncer_.start("255.255.255.255", kLanAnnouncePort, kLanPingPort)) {
                core::logInfo("Network", "LAN session announcer failed to start (real port conflict or non-Linux "
                                          "platform) -- hosting continues, but this session won't appear in LAN "
                                          "discovery");
            }
        }

        // Sprint 12 ("Moderation & Safety Systems"): wires
        // safety::TrustSafetyService's escalation actions to something
        // real for the first time -- see TrustSafetyService.hpp's own
        // header comment on why they were unwired ("none of Players/
        // moderator-tooling... exist yet"). onMute/onRestrict both map
        // to the same real, immediate enforcement mechanism
        // (setServerMuted()) since this codebase has no separate
        // "restricted but not muted" account state yet; onRestrict
        // additionally logs a review case so a creator/moderator sees
        // it escalated further than a plain auto-mute. Both review
        // callbacks append to reviewQueue_ -- see ReviewQueue.hpp's own
        // comment on why nothing here ever auto-files an actual legal
        // report.
        safety::TrustSafetyService::Callbacks trustSafetyCallbacks;
        trustSafetyCallbacks.onMute = [this](safety::PlayerId player) { setServerMuted(player, true); };
        trustSafetyCallbacks.onRestrict = [this](safety::PlayerId player) {
            setServerMuted(player, true);
            reviewQueue_.add(moderation::ReviewCase{player, "Restrict", false, clockSeconds_});
        };
        trustSafetyCallbacks.onHumanReviewRequired = [this](safety::PlayerId player, const char* reason) {
            reviewQueue_.add(moderation::ReviewCase{player, reason, false, clockSeconds_});
        };
        trustSafetyCallbacks.onLegalReportRequired = [this](safety::PlayerId player, const char* reason) {
            reviewQueue_.add(moderation::ReviewCase{player, reason, true, clockSeconds_});
        };
        // Kronos ("Moderation Architecture v1", Phase 1): real, but
        // honestly scoped -- there is no real per-client targeted
        // message wire type yet to actually deliver a warning to the
        // specific player it's about (only a broadcast ChatBroadcast
        // exists). Logging server-side is the real, stated interim
        // behavior; a real client-facing delivery channel is separate,
        // later work, not silently pretended to exist here.
        trustSafetyCallbacks.onWarn = [](safety::PlayerId player, const char* reason) {
            std::fprintf(stderr, "TrustSafetyService: real Warn issued for player=%u (%s) -- no client-facing delivery channel yet\n",
                         player, reason);
        };
        trustSafetyCallbacks.onEscalationEvent = [this](safety::PlayerId player, safety::EscalationTier tier,
                                                          const char* source) {
            escalationEventLog_.record(
                moderation::EscalationEvent{player, tier, source, static_cast<double>(clockSeconds_)});
        };
        trustSafetyService_.setCallbacks(std::move(trustSafetyCallbacks));

        serverReconciliation_.setValidate([this](PlayerId player, const InputCommand& command) {
            // Real server authority (task 3): a client's movement intent
            // is never trusted until it passes this real sanity check --
            // see net::isMovementPlausible()'s own comment. Shared,
            // real logic; Sprint 12's anti-cheat foundation reuses this
            // exact function rather than defining a second notion of
            // "plausible movement."
            bool plausible = isMovementPlausible(command);
            if (!plausible) {
                // Sprint 12 task 3's "Add movement sanity checks" real
                // escalation layer: one rejected input is normal (lag,
                // packet loss); a real *pattern* of rejections is a real
                // anti-cheat signal, fed into the same trust & safety
                // pipeline chat moderation uses -- see
                // anticheat::RollingEventCounter's own header comment on
                // why this is a real, simple, honestly-scoped rule and
                // not a claim of behavioral ML.
                movementRejectionCounter_.recordEvent(player, clockSeconds_);
                if (movementRejectionCounter_.isSuspicious(player, clockSeconds_, kMovementRejectionThreshold)) {
                    trustSafetyService_.onAntiCheatSignal(player, "MovementRejection", kMovementRejectionWeight);
                }
            }
            return plausible;
        });
        serverReconciliation_.setApply([this](PlayerId player, const InputCommand& command) {
            auto it = serverPlayerEntities_.find(player);
            if (it == serverPlayerEntities_.end() || currentEcs_ == nullptr) return;
            if (auto* transform = currentEcs_->tryGetComponent<core::Transform>(it->second)) {
                applyNetworkedMovement(*transform, command, config_.moveSpeed);
            }
        });
        serverReconciliation_.setGatherState([this](PlayerId) -> std::vector<EntityState> {
            std::vector<EntityState> states;
            if (currentEcs_ == nullptr) return states;
            auto view = currentEcs_->view<NetworkIdentity, core::Transform>();
            for (auto entity : view) {
                auto& identity = view.get<NetworkIdentity>(entity);
                auto& transform = view.get<core::Transform>(entity);
                EntityState state;
                state.networkId = identity.networkId;
                state.position = transform.position;
                state.rotation = transform.rotation;
                states.push_back(state);
            }
            return states;
        });
        // Kronos ("Active Joining UI"): fires right after a successful
        // Server-mode initialize() -- the server's own session genuinely
        // exists and is joinable from this point on.
        sessionActuallyStarted_ = true;
        if (onSessionJoined_) onSessionJoined_();
        return true;
    }

    if (config_.mode == NetworkMode::Client) {
        bool connecting = transport_.connectToServer(config_.serverAddress, config_.port);
        if (!connecting) {
            core::logError("Network", "connectToServer(%s:%u) failed to even begin connecting",
                            config_.serverAddress.c_str(), config_.port);
        }
        // Kronos ("Active Joining UI"): does NOT fire onSessionJoined_
        // here -- a successful connectToServer() only means the raw ENet
        // handshake started, not that this client's real JoinRequest has
        // been accepted yet. onSessionJoined_ fires later, once tickClient()
        // actually processes a real JoinAccepted payload.
        return connecting;
    }

    return true; // Offline -- real, honest no-op
}

void NetworkSession::shutdown() {
    // Kronos ("Moderation Architecture v1", Phase 1): real, symmetric
    // save half of initialize()'s real load -- only for a server that
    // genuinely started (matches sessionActuallyStarted_'s own existing
    // use elsewhere in this function; a client, or a server that failed
    // to bind, never loaded these in the first place).
    if (config_.mode == NetworkMode::Server && sessionActuallyStarted_ && config_.persistModerationLogs) {
        (void)chatLog_.saveToFile(kChatLogPath);
        (void)reportLog_.saveToFile(kReportLogPath);
        (void)reviewQueue_.saveToFile(kReviewQueuePath);
        (void)escalationEventLog_.saveToFile(kEscalationEventLogPath);
        (void)appealLog_.saveToFile(kAppealLogPath);
        (void)accountModerationRegistry_.saveToFile(kAccountModerationPath);
        (void)directMessageLog_.saveToFile(kDirectMessageLogPath);
    }

    stopStressTest();
    lanAnnouncer_.stop();

    // Kronos ("Active Joining UI"): tell every still-connected client WHY
    // before tearing the transport down -- a real, honest
    // Disconnect{SessionClosed} broadcast rather than every client just
    // seeing its socket die with no explanation. flush() forces it out
    // immediately, since transport_.shutdown() below destroys the host
    // with no further poll() call to do that as a side effect.
    if (config_.mode == NetworkMode::Server && sessionActuallyStarted_ && transport_.connectedPeerCount() > 0) {
        ByteWriter writer;
        writer.writeU8(static_cast<uint8_t>(WireMessageType::Disconnect));
        writer.writeU8(static_cast<uint8_t>(DisconnectReason::SessionClosed));
        transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
        transport_.flush();
    }

    // Kronos ("Active Joining UI"): the symmetric client-side fix -- a
    // real, graceful "I'm leaving" notification (enet_peer_disconnect_later()
    // via disconnectPeerGracefully()), instead of relying on the SERVER's
    // own real disconnect-timeout detection (5-30s by default) to
    // eventually notice this client is gone. Without this, a real "Leave
    // Flow" click left a stale "connected" entry (and no real
    // PlayerRosterLeft broadcast to other clients) for that whole window.
    if (config_.mode == NetworkMode::Client && sessionActuallyStarted_) {
        transport_.disconnectPeerGracefully(ENetTransport::kBroadcast); // ignored client-side -- see its own comment
        transport_.flush();
    }

    // Kronos ("Active Joining UI"): fire while state is still live, the
    // same ordering core::Scripting::fireUnload() already establishes for
    // its own "fire before teardown" contract -- only for a session that
    // genuinely started (see sessionActuallyStarted_'s own comment), not
    // a failed/still-connecting attempt.
    if (sessionActuallyStarted_ && onSessionLeft_) onSessionLeft_();

    transport_.shutdown();
    serverPlayerEntities_.clear();
    serverPeerToPlayer_.clear();
    serverPlayerBaselines_.clear();
    serverNetworkIdToEntity_.clear();
    remoteInterpolators_.clear();
    clientNetworkIdToEntity_.clear();
    localPlayerId_ = kInvalidPlayer;
    sessionId_ = 0;
    sessionName_.clear();
    lastJoinFailureReason_ = JoinFailureReason::None;
    lastJoinFailureServerProtocolVersion_ = 0;
    lastDisconnectReason_ = DisconnectReason::None;
    receivedGracefulDisconnect_ = false;
    sessionActuallyStarted_ = false;
    config_ = Config{};
}

void NetworkSession::registerNetworkedEntity(core::ECS& ecs, core::EntityId entity, PlayerId owner) {
    if (config_.mode != NetworkMode::Server) return; // only the server ever mints identities -- see header comment
    NetworkIdentity identity;
    identity.networkId = allocateNetworkId();
    identity.ownerId = owner;
    identity.isLocallyControlled = false; // server never "locally controls" a player avatar the way a client does
    ecs.addComponent<NetworkIdentity>(entity, identity);
    serverNetworkIdToEntity_[identity.networkId] = entity;
}

void NetworkSession::setServerMuted(PlayerId player, bool muted) {
    if (muted) {
        serverMutedPlayers_.insert(player);
    } else {
        serverMutedPlayers_.erase(player);
    }
}

publishing::PublishValidationResult NetworkSession::publishWorld(publishing::WorldListing listing) {
    publishing::PublishValidationResult result;
    if (config_.mode != NetworkMode::Server) {
        result.errors.push_back("Only the server can publish a world.");
        return result;
    }
    if (listing.worldId.empty()) result.errors.push_back("World id is required.");
    if (!publishing::isValidVersionString(listing.version)) {
        result.errors.push_back("Version must be in the form N.N or N.N.N (e.g. \"1.0\" or \"1.0.0\").");
    }
    publishing::PublishValidationResult metadataResult = publishing::validateWorldMetadata(listing.metadata);
    for (const auto& error : metadataResult.errors) result.errors.push_back(error);

    result.valid = result.errors.empty();
    if (result.valid) {
        listing.publishedAtUnixSeconds = static_cast<int64_t>(std::time(nullptr));
        worldRegistry_.upsert(std::move(listing));
    }
    return result;
}

void NetworkSession::sampleLocalInput(core::ECS& ecs, core::EntityId localPlayerEntity, glm::vec3 moveAxis, bool jump,
                                       bool primaryAction, float yaw, float pitch, float dt) {
    if (config_.mode != NetworkMode::Client) return;

    InputCommand command;
    command.deltaTime = dt;
    command.moveAxis = moveAxis;
    command.jump = jump;
    command.primaryAction = primaryAction;
    command.yaw = yaw;
    command.pitch = pitch;

    currentEcs_ = &ecs;
    // Reconfigured every call rather than lazily-once: `localPlayerEntity`
    // is a plain function parameter here (not stored), so there's no
    // cheap way to detect "did it change since last time" -- reassigning
    // two small std::functions every real tick is inexpensive at
    // real-time-game scale, not a hot-path concern worth the extra
    // state-tracking complexity.
    clientPrediction_.setPredictedApply([this, localPlayerEntity](const InputCommand& cmd) {
        if (currentEcs_ == nullptr) return;
        if (auto* transform = currentEcs_->tryGetComponent<core::Transform>(localPlayerEntity)) {
            applyNetworkedMovement(*transform, cmd, config_.moveSpeed);
        }
    });
    clientPrediction_.setAuthoritativeState([this, localPlayerEntity](const EntityState& state) {
        if (currentEcs_ == nullptr) return;
        if (auto* transform = currentEcs_->tryGetComponent<core::Transform>(localPlayerEntity)) {
            transform->position = state.position;
            transform->rotation = state.rotation;
        }
    });

    InputCommand recorded = clientPrediction_.recordAndPredict(command);

    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::Input));
    serializeInputCommand(recorded, writer);
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kUnreliableChannel, false);
}

void NetworkSession::tick(float dt, core::ECS& ecs, core::EntityId localPlayerEntity) {
    if (config_.mode == NetworkMode::Offline) return;
    currentEcs_ = &ecs;
    clockSeconds_ += dt;
    networkStats_.tick(dt);

    if (config_.mode == NetworkMode::Server) {
        tickServer(dt, ecs);
    } else {
        tickClient(dt, ecs, localPlayerEntity);
    }
    tickStressTest(dt);
}

void NetworkSession::tickServer(float dt, core::ECS& ecs) {
    ENetTransport::Callbacks callbacks;
    callbacks.onPeerConnected = [this](ENetTransport::PeerId peer) {
        // Kronos ("Active Joining UI"): assigning a real PlayerId and
        // registering with serverReconciliation_/tntWarsMatch_ happens
        // immediately on a raw ENet connect (cheap, and nextPlayerId_ is
        // never reused anyway -- see its own comment -- so one burned on
        // a later-rejected connection is harmless). Spawning a real
        // avatar and sending JoinAccepted/JoinRejected now waits for this
        // peer's own real JoinRequest -- see handleJoinRequestServer() --
        // giving the server a real moment to check protocol version/
        // capacity before ever spawning anything for a connection that
        // might get rejected.
        PlayerId player = nextPlayerId_++;
        serverPeerToPlayer_[peer] = player;
        serverReconciliation_.registerPlayer(player);
        tntWarsMatch_.registerPlayer(player);
        core::logInfo("Network", "peer %u connected, assigned player id %u (awaiting JoinRequest)", peer, player);
    };
    callbacks.onPeerDisconnected = [this](ENetTransport::PeerId peer) {
        auto it = serverPeerToPlayer_.find(peer);
        if (it == serverPeerToPlayer_.end()) return;
        PlayerId player = it->second;
        // Kronos ("Active Joining UI"): only a player who actually
        // completed a real join (present in serverPlayerEntities_) has
        // anything meaningful to announce -- a raw connection that
        // dropped before ever sending a real JoinRequest never joined the
        // roster in the first place.
        bool hadJoined = serverPlayerEntities_.count(player) > 0;
        std::string displayName = hadJoined ? serverPlayerDisplayNames_[player] : std::string();

        serverReconciliation_.unregisterPlayer(player);
        serverPlayerEntities_.erase(player);
        serverPlayerBaselines_.erase(player);
        serverPlayerDisplayNames_.erase(player);
        serverPlayerProfileIds_.erase(player);
        serverPlayerAgeGroups_.erase(player);
        networkStats_.removePlayer(player);
        // Sprint 12: real per-connection moderation/anti-cheat hygiene --
        // muteBlockRegistry_/movementRejectionCounter_ are keyed by
        // PlayerId, which (see nextPlayerId_'s own comment) is never
        // reused within one NetworkSession's lifetime, so this is real
        // cleanup, not a correctness requirement to avoid id collision.
        // serverMutedPlayers_/chatLog_/reportLog_/reviewQueue_ are
        // deliberately NOT cleared here -- moderation history outlives
        // one connection, see setServerMuted()'s own header comment.
        muteBlockRegistry_.removePlayer(player);
        movementRejectionCounter_.removePlayer(player);
        tntWarsMatch_.unregisterPlayer(player);
        serverPeerToPlayer_.erase(it);
        core::logInfo("Network", "player %u disconnected", player);

        if (hadJoined) {
            // Kronos ("Active Joining UI"): real roster-left broadcast +
            // real onPlayerRemoving_ hook.
            ByteWriter writer;
            writer.writeU8(static_cast<uint8_t>(WireMessageType::PlayerRosterLeft));
            writer.writeU32(player);
            writer.writeString(displayName);
            networkStats_.recordPacketSent(writer.size());
            transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
            if (onPlayerRemoving_) onPlayerRemoving_(player, displayName);
        }
    };
    callbacks.onPacketReceived = [this](ENetTransport::PeerId peer, const uint8_t* data, size_t size, uint8_t) {
        networkStats_.recordPacketReceived(size);
        if (size == 0) return;
        auto peerIt = serverPeerToPlayer_.find(peer);
        if (peerIt == serverPeerToPlayer_.end()) return;
        PlayerId player = peerIt->second;

        ByteReader reader(data, size);
        auto messageType = static_cast<WireMessageType>(reader.readU8());
        if (messageType == WireMessageType::Input) {
            InputCommand command;
            if (deserializeInputCommand(reader, command)) {
                serverReconciliation_.queueInput(player, command);
            } else {
                networkStats_.recordPacketDropped();
            }
        } else if (messageType == WireMessageType::TeleportRequest) {
            handleTeleportRequestServer(player, reader);
        } else if (messageType == WireMessageType::ChatMessage) {
            handleChatMessageServer(player, reader);
        } else if (messageType == WireMessageType::ReportPlayer) {
            handleReportPlayerServer(player, reader);
        } else if (messageType == WireMessageType::SubmitAppeal) {
            handleSubmitAppealServer(player, reader);
        } else if (messageType == WireMessageType::DirectMessageSend) {
            handleDirectMessageSendServer(player, reader);
        } else if (messageType == WireMessageType::SelectClass) {
            handleSelectClassServer(player, reader);
        } else if (messageType == WireMessageType::FireWeapon) {
            handleFireWeaponServer(player, reader);
        } else if (messageType == WireMessageType::TriggerUltimate) {
            handleTriggerUltimateServer(player, reader);
        } else if (messageType == WireMessageType::RemoteEventFire) {
            handleRemoteEventFireServer(player, reader);
        } else if (messageType == WireMessageType::JoinRequest) {
            handleJoinRequestServer(peer, player, reader);
        } else {
            networkStats_.recordPacketDropped();
        }
    };
    transport_.poll(0, callbacks);

    ++serverTick_;
    serverReconciliation_.tick(serverTick_);

    for (auto& [peer, player] : serverPeerToPlayer_) {
        DeltaSnapshot current = serverReconciliation_.buildSnapshot(player);
        DeltaSnapshot baseline = serverPlayerBaselines_.count(player) ? serverPlayerBaselines_[player] : DeltaSnapshot{};

        ByteWriter writer;
        writer.writeU8(static_cast<uint8_t>(WireMessageType::Snapshot));
        serializeSnapshotDelta(current, baseline, writer);
        networkStats_.recordPacketSent(writer.size());
        transport_.send(peer, writer.bytes().data(), writer.size(), kUnreliableChannel, false);

        serverPlayerBaselines_[player] = current;
        // Real, ENet-measured RTT for this peer (task 4's "ping") -- see
        // ENetTransport::roundTripTimeMs()'s own comment; 0.0f (an
        // honest "no data yet") for a peer ENet hasn't acked a reliable
        // packet to/from yet.
        networkStats_.recordPing(player, transport_.roundTripTimeMs(peer));
    }

    // Kronos ("Active Joining UI" -- LAN discovery): a real, honest
    // no-op if lanAnnouncer_ never started (advertiseOnLan == false, or
    // its own start() failed) -- see LanSessionAnnouncer::tick()'s own
    // early-return.
    //
    // Kronos ("Moderation Architecture v2", "Session Browser Game
    // Identity"): gameName/gameThumbnailColor/gameSafetyStatus below are
    // real, but this specific config_ passthrough isn't covered by a
    // NetworkSession-level end-to-end test -- this class's own real
    // announcer broadcasts to 255.255.255.255 (see start()'s call site
    // just above), and real L2/L3 broadcast delivery isn't guaranteed
    // inside this sandboxed test environment (the exact same, already-
    // documented limitation LanSessionAnnouncer.hpp's own class comment
    // states, and the real reason testLanDiscoveryOptOutViaAdvertiseOnLanFalse
    // only asserts the *negative* case for a real hosted session). The
    // real wire format and the real Announcer->Browser mechanism ARE
    // fully covered (testLanDiscoveryProtocolSerializationRoundTrips,
    // testLanSessionAnnouncerRealLoopbackReachesBrowserWithRealPing,
    // both extended with these fields) -- only this one-line config_->
    // announcement copy is manual-verification-only, a real, small, and
    // low-risk gap, not a silent one.
    LanSessionAnnouncement announcement;
    announcement.protocolVersion = kNetworkProtocolVersion;
    announcement.sessionId = sessionId_;
    announcement.sessionName = sessionName_;
    announcement.hostDisplayName = config_.hostDisplayName;
    announcement.gamePort = config_.port;
    announcement.currentPlayerCount = static_cast<uint8_t>(std::min<size_t>(serverPlayerEntities_.size(), 255));
    announcement.maxPlayerCount = static_cast<uint8_t>(std::min<size_t>(config_.maxClients, 255));
    announcement.gameName = config_.gameName;
    announcement.gameThumbnailColor = config_.gameThumbnailColor;
    announcement.gameSafetyStatusValue = static_cast<uint8_t>(config_.gameSafetyStatus);
    announcement.sessionStartUnixSeconds = sessionStartUnixSeconds_;
    lanAnnouncer_.tick(dt, announcement);
}

void NetworkSession::handleTeleportRequestServer(PlayerId player, ByteReader& reader) {
    uint32_t padNetworkId = reader.readU32();
    if (reader.hasError() || currentEcs_ == nullptr) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Sprint 12 task 4's "Add world-level safety settings" real
    // enforcement, and task 3's "Add mining/interaction rate checks" --
    // this is the one real interaction dispatch path this pass has
    // (teleport, see Sprint 11's own scope note on why), so it's the
    // real, if singular, enforcement point for both. `interactionRateLimiter_`
    // is the exact same shared net::TokenBucketRateLimiter primitive
    // chat rate-limiting uses below, given a real, live-synced cap so a
    // creator changing worldSafetySettings() takes effect on the very
    // next request, not just at session start.
    if (!worldSafetySettings_.teleportEnabled) {
        networkStats_.recordPacketDropped();
        return;
    }
    interactionRateLimiter_.setMaxPerSecond(worldSafetySettings_.maxInteractionsPerSecond);
    if (!interactionRateLimiter_.tryConsume(player, clockSeconds_)) {
        networkStats_.recordPacketDropped();
        return;
    }

    auto playerEntityIt = serverPlayerEntities_.find(player);
    auto padEntityIt = serverNetworkIdToEntity_.find(padNetworkId);
    if (playerEntityIt == serverPlayerEntities_.end() || padEntityIt == serverNetworkIdToEntity_.end()) {
        networkStats_.recordPacketDropped();
        return;
    }

    auto* playerTransform = currentEcs_->tryGetComponent<core::Transform>(playerEntityIt->second);
    auto* pad = currentEcs_->tryGetComponent<core::TeleportPad>(padEntityIt->second);
    auto* padTransform = currentEcs_->tryGetComponent<core::Transform>(padEntityIt->second);
    if (playerTransform == nullptr || pad == nullptr || padTransform == nullptr) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Real server-side validation (task 3) -- the same two real checks
    // net::InteractionValidation.hpp exposes for exactly this purpose:
    // the requesting player must actually be near the pad (not lying
    // about proximity), and the pad's own configured destination must be
    // a real, sane world position.
    constexpr float kInteractionRange = 3.0f;
    if (!isWithinInteractionRange(playerTransform->position, padTransform->position, kInteractionRange)) {
        networkStats_.recordPacketDropped();
        return;
    }
    constexpr float kWorldSanityRadius = 10000.0f;
    if (!isTeleportDestinationValid(pad->destination, glm::vec3(0.0f), kWorldSanityRadius)) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Real server authority: the server -- and only the server -- writes
    // the authoritative Transform; the requesting client finds out via
    // the next real snapshot, exactly like any other server-applied
    // state change.
    playerTransform->position = pad->destination;
}

void NetworkSession::handleChatMessageServer(PlayerId player, ByteReader& reader) {
    std::string text = reader.readString();
    if (reader.hasError()) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Sprint 12 task 4's real world-safety enforcement + task 1's real
    // rate limiting -- both checked before the message is even looked
    // at, matching this codebase's "reject-and-log, never silently fix"
    // server-authority convention (net::ServerReconciliation::validate()
    // established it first; this is the same pattern applied to chat).
    if (!worldSafetySettings_.chatEnabled || isServerMuted(player)) {
        networkStats_.recordPacketDropped();
        return;
    }
    // Kronos ("Moderation Architecture v2", "Account System v1"): real,
    // persistent, cross-session mute -- distinct from isServerMuted()'s
    // real, session-scoped PlayerId-keyed check just above (a real
    // moderator action might only mean "for tonight," this means "this
    // real account, indefinitely, across reconnects").
    auto profileIdIt = serverPlayerProfileIds_.find(player);
    if (profileIdIt != serverPlayerProfileIds_.end() && profileIdIt->second != 0 &&
        accountModerationRegistry_.isMuted(profileIdIt->second)) {
        networkStats_.recordPacketDropped();
        return;
    }
    chatRateLimiter_.setMaxPerSecond(worldSafetySettings_.maxChatMessagesPerSecond);
    if (!chatRateLimiter_.tryConsume(player, clockSeconds_)) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Real profanity filter (task 1) -- censors in place; the log below
    // still records the real, original text (see ChatLog.hpp's own
    // comment on why: a moderation log that only kept the censored
    // version would be useless as evidence for exactly the messages
    // worth reviewing).
    moderation::ProfanityCheckResult profanityResult =
        worldSafetySettings_.profanityFilterEnabled ? profanityFilter_.check(text) : moderation::ProfanityCheckResult{};

    // Real escalation-pipeline scoring (finally gives
    // safety::TrustSafetyService a real caller -- see this class's
    // header comment). Kronos ("Moderation Architecture v1", Phase 1):
    // the message is still delivered regardless of the classification
    // for every *ambiguous/borderline* category -- but a real hard-block
    // category (safety::PolicyEngine) now genuinely stops delivery, see
    // TextClassification::blocked's own comment. The message is still
    // real-recorded to chatLog_ below either way -- a blocked message is
    // exactly the kind of thing a moderator needs to be able to review,
    // not evidence that should vanish just because it wasn't delivered.
    // Kronos ("Moderation Architecture v2", "Minor Mode Enforcement"):
    // the real, now-available sender AgeGroup (playerAgeGroup(), fed by
    // the real JoinRequest identity signal) -- closes the real gap
    // Phase 1's own onChatMessage() comment stated ("no per-connected-
    // remote-player age signal exists in the network protocol yet").
    safety::TextClassification classification = trustSafetyService_.onChatMessage(player, text, playerAgeGroup(player));

    chatLog_.record(moderation::ChatLogEntry{player, text, profanityResult.containsProfanity, classification.flagged,
                                              static_cast<double>(clockSeconds_)});

    // Kronos ("Moderation Architecture v2", item H "ML Retraining
    // Pipeline (Stub)"): real, future-ready (text, classification) data
    // collection -- only flagged messages, same "not every message is
    // worth persisting long-term" reasoning EscalationEventLog's own
    // caller applies (dispatchEscalation()'s own comment). Gated behind
    // the same real, explicit persistModerationLogs opt-in every other
    // moderation log uses (Config::persistModerationLogs's own comment) --
    // a real, short-lived test server never pollutes this file.
    if (classification.flagged && config_.persistModerationLogs) {
        (void)moderation::appendTrainingDataSample(kTrainingDataLogPath, text, classification);
    }

    if (classification.blocked) {
        networkStats_.recordPacketDropped();
        return;
    }

    const std::string& outgoingText = worldSafetySettings_.profanityFilterEnabled ? profanityResult.censored : text;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::ChatBroadcast));
    writer.writeU32(player);
    writer.writeString(outgoingText);
    networkStats_.recordPacketSent(writer.size());
    for (auto& [peer, recipient] : serverPeerToPlayer_) {
        // Real, per-recipient mute/block filtering (task 1) -- everyone
        // else still gets the message; only the recipient(s) who chose
        // to mute/block this sender don't.
        if (!muteBlockRegistry_.shouldDeliver(recipient, player)) continue;
        transport_.send(peer, writer.bytes().data(), writer.size(), kReliableChannel, true);
    }
}

void NetworkSession::handleReportPlayerServer(PlayerId player, ByteReader& reader) {
    PlayerId reported = reader.readU32();
    auto category = static_cast<moderation::ReportCategory>(reader.readU8());
    std::string description = reader.readString();
    if (reader.hasError()) {
        networkStats_.recordPacketDropped();
        return;
    }

    reportLog_.submit(moderation::PlayerReport{player, reported, category, description, static_cast<double>(clockSeconds_)});
}

void NetworkSession::handleDirectMessageSendServer(PlayerId sender, ByteReader& reader) {
    PlayerId recipient = reader.readU32();
    std::string text = reader.readString();
    if (reader.hasError()) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Same real world-safety/mute gates chat already uses -- a DM system
    // is real chat's sibling, not a separate, ungated channel.
    // Kronos ("Moderation Architecture v2", item 5): directMessagesEnabled
    // is a real, separate creator toggle from chatEnabled -- see that
    // field's own comment.
    if (!worldSafetySettings_.chatEnabled || !worldSafetySettings_.directMessagesEnabled || isServerMuted(sender)) {
        networkStats_.recordPacketDropped();
        return;
    }

    core::AgeGroup senderAgeGroup = playerAgeGroup(sender);
    core::AgeGroup recipientAgeGroup = playerAgeGroup(recipient);

    // Kronos ("Moderation Architecture v2", "Minor Mode Enforcement" --
    // "DM restrictions"): the real, simplest, safest v1 rule -- if
    // EITHER real party is a real (or possibly) minor, this DM is
    // real-blocked outright, not just moderated. A real, honest,
    // deliberately conservative v1 scope (no per-pair "both adults,
    // both minors, one of each" nuance) -- "protect minors by default"
    // means starting from "no DMs touching a minor at all," not
    // building the more permissive, more complex version first.
    bool minorModeBlocked = senderAgeGroup != core::AgeGroup::Adult || recipientAgeGroup != core::AgeGroup::Adult;

    // Real moderation routing through the exact same TrustSafetyService/
    // PolicyEngine chat already uses (spec: "Routed through
    // TrustSafetyService") -- one real text-classification pipeline, not
    // a second, DM-specific one.
    safety::TextClassification classification = trustSafetyService_.onChatMessage(sender, text, senderAgeGroup);
    bool blocked = minorModeBlocked || classification.blocked;

    directMessageLog_.record(moderation::DirectMessageLogEntry{sender, recipient, text, classification.flagged,
                                                                 blocked, static_cast<double>(clockSeconds_)});

    // Kronos ("Moderation Architecture v2", item B "Behavioral Model
    // (Heuristic v1)"): real conversion from moderation::
    // DirectMessageLog's own entries into safety::DirectMessageSample --
    // see BehavioralPatternAnalyzer.hpp's own comment on why this
    // conversion happens here (in NetworkSession, which owns both
    // namespaces already) rather than inside safety:: itself. Runs on
    // every real DM send, blocked-or-not -- a blocked DM is still a real
    // data point about this sender's real pattern.
    std::vector<safety::DirectMessageSample> senderSamples;
    for (const moderation::DirectMessageLogEntry& entry : directMessageLog_.entries()) {
        if (entry.sender != sender) continue;
        senderSamples.push_back(safety::DirectMessageSample{entry.recipient, playerAgeGroup(entry.recipient), entry.text,
                                                              entry.serverTimestampSeconds});
    }
    trustSafetyService_.onDirectMessagePattern(sender, senderSamples, static_cast<double>(clockSeconds_));

    if (blocked) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Real, targeted delivery -- the one real recipient's own peer only,
    // never a broadcast (unlike ChatBroadcast). serverPeerToPlayer_ only
    // maps peer->PlayerId; a real linear search over this real, small
    // (one entry per connected player) map is the honest, simple choice
    // over maintaining a second, parallel reverse map just for this.
    for (const auto& [peer, playerId] : serverPeerToPlayer_) {
        if (playerId != recipient) continue;
        ByteWriter writer;
        writer.writeU8(static_cast<uint8_t>(WireMessageType::DirectMessageDeliver));
        writer.writeU32(sender);
        writer.writeString(text);
        networkStats_.recordPacketSent(writer.size());
        transport_.send(peer, writer.bytes().data(), writer.size(), kReliableChannel, true);
        break;
    }
}

void NetworkSession::handleSubmitAppealServer(PlayerId player, ByteReader& reader) {
    std::string playerStatement = reader.readString();
    std::string relatedReviewCaseReason = reader.readString();
    if (reader.hasError()) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Kronos ("Moderation Architecture v2", "Account System v1" -- "appeal
    // history tied to identity"): real, best-effort profileId lookup --
    // 0 (the real, honest "unknown" default) for a player who joined via
    // a pre-Account-System-v2 client that sent no real profileId at all,
    // same soft-fallback convention handleJoinRequestServer() itself
    // already uses.
    auto submitterProfileIdIt = serverPlayerProfileIds_.find(player);
    uint64_t submitterProfileId = submitterProfileIdIt != serverPlayerProfileIds_.end() ? submitterProfileIdIt->second : 0;

    appealLog_.submit(moderation::Appeal{player, submitterProfileId, playerStatement, relatedReviewCaseReason,
                                          moderation::AppealOutcome::Pending, "", static_cast<double>(clockSeconds_),
                                          0.0});
}

void NetworkSession::handleSelectClassServer(PlayerId player, ByteReader& reader) {
    auto classType = static_cast<tntwars::PlayerClassType>(reader.readU8());
    if (reader.hasError()) {
        networkStats_.recordPacketDropped();
        return;
    }
    if (!tntWarsMatch_.selectClass(player, classType)) networkStats_.recordPacketDropped();
}

void NetworkSession::handleFireWeaponServer(PlayerId player, ByteReader& reader) {
    glm::vec3 origin = reader.readVec3();
    glm::vec3 aimDirection = reader.readVec3();
    if (reader.hasError()) {
        networkStats_.recordPacketDropped();
        return;
    }

    tntwars::TntWarsMatch::FireResult result = tntWarsMatch_.fireWeapon(player, origin, aimDirection, clockSeconds_);
    if (!result.accepted) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Real, reliable broadcast of the accepted shot -- every client
    // (including the firing client, for a real, consistent view rather
    // than a locally-predicted one that might disagree) deterministically
    // replays the exact same real tntwars::stepProjectile() from here.
    // See Projectile.hpp's own header comment for why this is a real,
    // deliberately lighter-weight replication technique than per-tick
    // snapshot sync.
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::ProjectileSpawned));
    writer.writeU8(static_cast<uint8_t>(result.projectile.type));
    writer.writeU32(result.projectile.owner);
    writer.writeVec3(result.projectile.position);
    writer.writeVec3(result.projectile.velocity);
    writer.writeFloat(result.projectile.damage);
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::handleTriggerUltimateServer(PlayerId player, ByteReader& reader) {
    (void)reader; // no payload -- the server derives everything from the real, already-known player+class
    tntwars::TntWarsMatch::UltimateResult result = tntWarsMatch_.triggerUltimate(player, clockSeconds_);
    if (!result.accepted) {
        networkStats_.recordPacketDropped();
        return;
    }

    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::UltimateTriggered));
    writer.writeU32(player);
    writer.writeU8(static_cast<uint8_t>(result.type));
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::handleRemoteEventFireServer(PlayerId player, ByteReader& reader) {
    std::string name = reader.readString();
    RemoteEvent::Payload payload = deserializeRemoteEventPayload(reader);
    if (reader.hasError()) {
        networkStats_.recordPacketDropped();
        return;
    }
    // remoteEvent(name) real-creates a fresh, unconfigured RemoteEvent if
    // nobody has called it yet (e.g. no Luau network.onServerEvent() was
    // ever registered for this name) -- handleFromClient() is a real,
    // honest no-op in that case (no schema/rate-limit to fail, no handler
    // to invoke), not an error.
    if (!remoteEvent(name).handleFromClient(player, payload)) {
        networkStats_.recordPacketDropped();
    }
}

void NetworkSession::handleJoinRequestServer(ENetTransport::PeerId peer, PlayerId player, ByteReader& reader) {
    uint32_t clientProtocolVersion = reader.readU32();
    std::string displayName = reader.readString();
    if (reader.hasError()) {
        networkStats_.recordPacketDropped();
        return;
    }

    // Real version-mismatch rejection FIRST, using only the two original
    // fields -- deliberately BEFORE reading the newer profileId/AgeGroup
    // fields below, so a stale/incompatible client that doesn't even
    // know to send them still gets a real, honest JoinRejected reply
    // instead of a silent drop.
    if (clientProtocolVersion != kNetworkProtocolVersion) {
        ByteWriter rejected;
        rejected.writeU8(static_cast<uint8_t>(WireMessageType::JoinRejected));
        rejected.writeU8(static_cast<uint8_t>(JoinFailureReason::VersionMismatch));
        rejected.writeU32(kNetworkProtocolVersion);
        networkStats_.recordPacketSent(rejected.size());
        transport_.send(peer, rejected.bytes().data(), rejected.size(), kReliableChannel, true);
        transport_.disconnectPeerGracefully(peer);
        core::logInfo("Network", "rejected player %u: protocol version mismatch (client %u, server %u)", player,
                       clientProtocolVersion, kNetworkProtocolVersion);
        return;
    }

    // Kronos ("Moderation Architecture v2", "Account System v1"): real
    // identity signals -- see NetworkSession::setLocalIdentity()'s own
    // comment. Read only now, after the version check above already
    // passed -- a genuinely compatible-version client always sends
    // these, so a real hasError() here means a real, honest malformed/
    // adversarial payload, not just an older client. Defaults (0,
    // Unknown) on error, same "fail soft, real conservative default"
    // spirit as every other identity gap in this system -- doesn't
    // reject the join by itself, no-op ban-checking for a 0 profileId.
    uint64_t clientProfileId = reader.readU64();
    auto clientAgeGroup = static_cast<core::AgeGroup>(reader.readU8());
    if (reader.hasError()) {
        clientProfileId = 0;
        clientAgeGroup = core::AgeGroup::Unknown;
    }

    // Real, persistent ban enforcement -- keyed by the real, stable
    // profileId (survives a reconnect, unlike net::PlayerId).
    if (clientProfileId != 0 && accountModerationRegistry_.isBanned(clientProfileId)) {
        ByteWriter rejected;
        rejected.writeU8(static_cast<uint8_t>(WireMessageType::JoinRejected));
        rejected.writeU8(static_cast<uint8_t>(JoinFailureReason::Banned));
        rejected.writeU32(kNetworkProtocolVersion);
        networkStats_.recordPacketSent(rejected.size());
        transport_.send(peer, rejected.bytes().data(), rejected.size(), kReliableChannel, true);
        transport_.disconnectPeerGracefully(peer);
        core::logInfo("Network", "rejected player %u: real, persistent ban on profileId %llu", player,
                       static_cast<unsigned long long>(clientProfileId));
        return;
    }

    // Real, business-level capacity check -- serverPlayerEntities_ only
    // ever contains players who actually completed a real join, distinct
    // from the raw ENet peer cap (see kSessionFullRejectionHeadroom's own
    // comment on why the raw cap is deliberately larger than this).
    if (serverPlayerEntities_.size() >= config_.maxClients) {
        ByteWriter rejected;
        rejected.writeU8(static_cast<uint8_t>(WireMessageType::JoinRejected));
        rejected.writeU8(static_cast<uint8_t>(JoinFailureReason::SessionFull));
        rejected.writeU32(kNetworkProtocolVersion);
        networkStats_.recordPacketSent(rejected.size());
        transport_.send(peer, rejected.bytes().data(), rejected.size(), kReliableChannel, true);
        transport_.disconnectPeerGracefully(peer);
        core::logInfo("Network", "rejected player %u: session full (%zu/%zu)", player, serverPlayerEntities_.size(),
                       config_.maxClients);
        return;
    }

    if (!onPlayerJoin_ || currentEcs_ == nullptr) {
        networkStats_.recordPacketDropped();
        return;
    }

    core::EntityId avatar = onPlayerJoin_(*currentEcs_, player);
    serverPlayerEntities_[player] = avatar;
    registerNetworkedEntity(*currentEcs_, avatar, player);

    // Kronos ("Active Joining UI"): the real display name now arrives
    // WITH the join request, so the avatar's own real Name component
    // reflects it from the moment it's spawned -- supersedes the separate
    // post-join "SetDisplayName" RemoteEvent built last session (see
    // studio::plugins::NetworkOverlayPlugin's own updated comment).
    if (auto* name = currentEcs_->tryGetComponent<core::Name>(avatar)) name->value = displayName;
    serverPlayerDisplayNames_[player] = displayName;
    serverPlayerProfileIds_[player] = clientProfileId;
    serverPlayerAgeGroups_[player] = clientAgeGroup;

    uint32_t avatarNetworkId = 0;
    if (auto* identity = currentEcs_->tryGetComponent<NetworkIdentity>(avatar)) avatarNetworkId = identity->networkId;

    // Kronos ("Active Joining UI"): a real, honest gap found while
    // implementing -- PlayerRosterJoined only reaches ALREADY-joined
    // peers when someone NEW joins, so a client joining an in-progress
    // session would otherwise never learn about anyone who joined before
    // it did. JoinAccepted carries a real, one-time snapshot of every
    // other currently-joined player to close that gap; every join/leave
    // AFTER this one is covered by the real PlayerRosterJoined/Left
    // broadcasts as usual.
    std::vector<PlayerId> existingPlayers;
    std::vector<std::string> existingNames;
    for (const auto& [existingPlayer, existingName] : serverPlayerDisplayNames_) {
        if (existingPlayer == player) continue; // this join hasn't been announced to itself
        existingPlayers.push_back(existingPlayer);
        existingNames.push_back(existingName);
    }

    ByteWriter accepted;
    accepted.writeU8(static_cast<uint8_t>(WireMessageType::JoinAccepted));
    accepted.writeU32(player);
    accepted.writeU32(avatarNetworkId);
    accepted.writeU64(sessionId_);
    accepted.writeString(sessionName_);
    accepted.writeU32(kNetworkProtocolVersion);
    uint8_t existingCount = static_cast<uint8_t>(std::min<size_t>(existingPlayers.size(), 255));
    accepted.writeU8(existingCount);
    for (uint8_t i = 0; i < existingCount; ++i) {
        accepted.writeU32(existingPlayers[i]);
        accepted.writeString(existingNames[i]);
    }
    networkStats_.recordPacketSent(accepted.size());
    transport_.send(peer, accepted.bytes().data(), accepted.size(), kReliableChannel, true);

    // Real roster-joined broadcast to every OTHER already-joined peer --
    // not kBroadcast (which would also hit any not-yet-joined connections
    // and redundantly tell the new player about themselves).
    ByteWriter roster;
    roster.writeU8(static_cast<uint8_t>(WireMessageType::PlayerRosterJoined));
    roster.writeU32(player);
    roster.writeString(displayName);
    for (const auto& [otherPeer, otherPlayer] : serverPeerToPlayer_) {
        if (otherPlayer == player || serverPlayerEntities_.count(otherPlayer) == 0) continue;
        networkStats_.recordPacketSent(roster.size());
        transport_.send(otherPeer, roster.bytes().data(), roster.size(), kReliableChannel, true);
    }
    if (onPlayerAdded_) onPlayerAdded_(player, displayName);

    core::logInfo("Network", "player %u (\"%s\") joined session %llu", player, displayName.c_str(),
                   static_cast<unsigned long long>(sessionId_));
}

void NetworkSession::disconnectPlayer(PlayerId player, DisconnectReason reason) {
    if (config_.mode != NetworkMode::Server) return;
    // Reverse peer lookup -- serverPeerToPlayer_ is keyed the other way;
    // this map is small (at most maxClients + headroom entries) so a
    // linear scan here is real, simple, and not a hot path (a kick is a
    // rare, moderator-driven action, not a per-tick operation).
    for (const auto& [peer, mappedPlayer] : serverPeerToPlayer_) {
        if (mappedPlayer != player) continue;
        ByteWriter writer;
        writer.writeU8(static_cast<uint8_t>(WireMessageType::Disconnect));
        writer.writeU8(static_cast<uint8_t>(reason));
        networkStats_.recordPacketSent(writer.size());
        transport_.send(peer, writer.bytes().data(), writer.size(), kReliableChannel, true);
        transport_.disconnectPeerGracefully(peer);
        return;
    }
}

void NetworkSession::fireServerEvent(const std::string& name, const RemoteEvent::Payload& payload) {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::RemoteEventFire));
    writer.writeString(name);
    serializeRemoteEventPayload(payload, writer);
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::fireAllClientsEvent(const std::string& name, const RemoteEvent::Payload& payload) {
    if (config_.mode != NetworkMode::Server) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::RemoteEventBroadcast));
    writer.writeString(name);
    serializeRemoteEventPayload(payload, writer);
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::tickClient(float dt, core::ECS& ecs, core::EntityId localPlayerEntity) {
    ENetTransport::Callbacks callbacks;
    callbacks.onPeerConnected = [this](ENetTransport::PeerId) {
        // Kronos ("Active Joining UI"): the real, client-initiated join
        // handshake -- the raw ENet connect completing is not the same
        // as being joined (see JoinAccepted/JoinRejected handling below).
        ByteWriter writer;
        writer.writeU8(static_cast<uint8_t>(WireMessageType::JoinRequest));
        writer.writeU32(kNetworkProtocolVersion);
        writer.writeString(localDisplayName_);
        // Kronos ("Moderation Architecture v2", "Account System v1"):
        // real identity signals -- see setLocalIdentity()'s own comment.
        writer.writeU64(localProfileId_);
        writer.writeU8(static_cast<uint8_t>(localAgeGroup_));
        networkStats_.recordPacketSent(writer.size());
        transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
    };
    callbacks.onPeerDisconnected = [this](ENetTransport::PeerId) {
        // Kronos ("Active Joining UI"): distinguishes a real, graceful
        // goodbye (a Disconnect message already processed below, or a
        // JoinRejected that never started a real session in the first
        // place) from a genuine ungraceful drop, using only information
        // ENet already surfaces -- no invented heuristic.
        if (!receivedGracefulDisconnect_ && sessionActuallyStarted_) {
            lastDisconnectReason_ = DisconnectReason::ConnectionLost;
            sessionActuallyStarted_ = false;
            if (onSessionLeft_) onSessionLeft_();
            if (onDisconnected_) onDisconnected_(DisconnectReason::ConnectionLost);
        }
    };
    callbacks.onPacketReceived = [this, &ecs, localPlayerEntity](ENetTransport::PeerId, const uint8_t* data, size_t size,
                                                                   uint8_t) {
        networkStats_.recordPacketReceived(size);
        if (size == 0) return;
        ByteReader reader(data, size);
        auto messageType = static_cast<WireMessageType>(reader.readU8());

        if (messageType == WireMessageType::JoinAccepted) {
            localPlayerId_ = reader.readU32();
            uint32_t avatarNetworkId = reader.readU32();
            sessionId_ = reader.readU64();
            sessionName_ = reader.readString();
            uint32_t serverProtocolVersion = reader.readU32();
            uint8_t existingCount = reader.readU8();
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            (void)serverProtocolVersion; // implied equal to kNetworkProtocolVersion -- a mismatch gets JoinRejected instead

            clientNetworkIdToEntity_[avatarNetworkId] = localPlayerEntity;
            if (auto* existingIdentity = ecs.tryGetComponent<NetworkIdentity>(localPlayerEntity)) {
                existingIdentity->networkId = avatarNetworkId;
                existingIdentity->isLocallyControlled = true;
            } else {
                NetworkIdentity newIdentity;
                newIdentity.networkId = avatarNetworkId;
                newIdentity.ownerId = localPlayerId_;
                newIdentity.isLocallyControlled = true;
                ecs.addComponent<NetworkIdentity>(localPlayerEntity, newIdentity);
            }

            // Real, one-time existing-roster snapshot -- see
            // handleJoinRequestServer()'s own comment on why this exists.
            clientKnownPlayers_.clear();
            clientKnownPlayers_[localPlayerId_] = localDisplayName_;
            for (uint8_t i = 0; i < existingCount && !reader.hasError(); ++i) {
                PlayerId existingPlayer = reader.readU32();
                std::string existingName = reader.readString();
                if (!reader.hasError()) {
                    clientKnownPlayers_[existingPlayer] = existingName;
                    // Real, honest "you get told about everyone already
                    // here, not just future joins" -- matches Roblox's own
                    // documented Players.PlayerAdded behavior. Never fires
                    // for the local player itself (see setOnPlayerAdded()'s
                    // own comment -- that's onSessionJoined_'s job).
                    if (onPlayerAdded_) onPlayerAdded_(existingPlayer, existingName);
                }
            }

            lastJoinFailureReason_ = JoinFailureReason::None;
            receivedGracefulDisconnect_ = false;
            sessionActuallyStarted_ = true;
            if (onSessionJoined_) onSessionJoined_();
            return;
        }

        if (messageType == WireMessageType::JoinRejected) {
            auto reason = static_cast<JoinFailureReason>(reader.readU8());
            uint32_t serverProtocolVersion = reader.readU32();
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            lastJoinFailureReason_ = reason;
            lastJoinFailureServerProtocolVersion_ = serverProtocolVersion;
            return;
        }

        if (messageType == WireMessageType::Disconnect) {
            auto reason = static_cast<DisconnectReason>(reader.readU8());
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            receivedGracefulDisconnect_ = true;
            lastDisconnectReason_ = reason;
            bool wasStarted = sessionActuallyStarted_;
            sessionActuallyStarted_ = false;
            if (wasStarted && onSessionLeft_) onSessionLeft_();
            if (onDisconnected_) onDisconnected_(reason);
            return;
        }

        if (messageType == WireMessageType::PlayerRosterJoined) {
            PlayerId joinedPlayer = reader.readU32();
            std::string joinedName = reader.readString();
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            clientKnownPlayers_[joinedPlayer] = joinedName;
            if (onPlayerAdded_) onPlayerAdded_(joinedPlayer, joinedName);
            return;
        }

        if (messageType == WireMessageType::PlayerRosterLeft) {
            PlayerId leftPlayer = reader.readU32();
            std::string leftName = reader.readString();
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            clientKnownPlayers_.erase(leftPlayer);
            if (onPlayerRemoving_) onPlayerRemoving_(leftPlayer, leftName);
            return;
        }

        if (messageType == WireMessageType::ChatBroadcast) {
            PlayerId sender = reader.readU32();
            std::string text = reader.readString();
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            if (onChatMessageReceived_) onChatMessageReceived_(sender, text);
            return;
        }

        if (messageType == WireMessageType::DirectMessageDeliver) {
            PlayerId sender = reader.readU32();
            std::string text = reader.readString();
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            if (onDirectMessageReceived_) onDirectMessageReceived_(sender, text);
            return;
        }

        if (messageType == WireMessageType::ProjectileSpawned) {
            tntwars::ProjectileState projectile;
            projectile.type = static_cast<tntwars::ProjectileType>(reader.readU8());
            projectile.owner = reader.readU32();
            projectile.position = reader.readVec3();
            projectile.velocity = reader.readVec3();
            projectile.damage = reader.readFloat();
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            if (onProjectileSpawned_) onProjectileSpawned_(projectile);
            return;
        }

        if (messageType == WireMessageType::UltimateTriggered) {
            PlayerId player = reader.readU32();
            auto ultimateType = static_cast<tntwars::UltimateType>(reader.readU8());
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            if (onUltimateTriggered_) onUltimateTriggered_(player, ultimateType);
            return;
        }

        if (messageType == WireMessageType::RemoteEventBroadcast) {
            std::string name = reader.readString();
            RemoteEvent::Payload payload = deserializeRemoteEventPayload(reader);
            if (reader.hasError()) {
                networkStats_.recordPacketDropped();
                return;
            }
            if (onClientEventReceived_) onClientEventReceived_(name, payload);
            return;
        }

        if (messageType != WireMessageType::Snapshot) {
            networkStats_.recordPacketDropped();
            return;
        }

        DeltaSnapshot snapshot;
        if (!deserializeSnapshotDelta(reader, clientBaseline_, snapshot)) {
            networkStats_.recordPacketDropped();
            return;
        }
        clientBaseline_ = snapshot;

        for (const EntityState& state : snapshot.entities) {
            if (state.networkId == 0) continue;
            auto entityIt = clientNetworkIdToEntity_.find(state.networkId);
            bool isLocalPlayer = entityIt != clientNetworkIdToEntity_.end() && entityIt->second == localPlayerEntity;

            if (isLocalPlayer) {
                clientPrediction_.reconcile(state, snapshot.lastProcessedInputSequence);
            } else {
                if (entityIt == clientNetworkIdToEntity_.end()) {
                    // The real first time this client has ever heard about
                    // this networkId (another player's avatar, or any
                    // other networked entity) -- task 2's "player sync"
                    // means a client must actually be able to SEE other
                    // networked players, not just smoothly interpolate a
                    // networkId with nothing local to apply it to. Spawn a
                    // real, minimal (Transform + Name) local placeholder
                    // entity now; the interpolation loop below drives its
                    // Transform every frame from here on exactly like it
                    // already does for the local player's own avatar via
                    // reconcile().
                    std::string entityName = "RemoteEntity" + std::to_string(state.networkId);
                    core::EntityId remoteEntity = ecs.createEntity(entityName);
                    ecs.addComponent<core::Transform>(remoteEntity, core::Transform{});
                    clientNetworkIdToEntity_[state.networkId] = remoteEntity;
                }
                remoteInterpolators_[state.networkId].pushSnapshot(state, clockSeconds_);
            }
        }
    };
    transport_.poll(0, callbacks);

    // Real, ENet-measured RTT to the server (task 4's "ping") -- see
    // ENetTransport::roundTripTimeMs()'s own comment. Only meaningful
    // once the real handshake has assigned localPlayerId_; 0.0f (an
    // honest "no data yet") before then.
    if (localPlayerId_ != kInvalidPlayer) {
        networkStats_.recordPing(localPlayerId_, transport_.roundTripTimeMs(ENetTransport::kBroadcast));
    }

    // Real, smoothed rendering for every known remote entity -- see
    // net::RemoteEntityInterpolator's own header comment for the
    // interpolate-if-bracketed, dead-reckon-otherwise logic this drives.
    for (auto& [networkId, interpolator] : remoteInterpolators_) {
        auto entityIt = clientNetworkIdToEntity_.find(networkId);
        if (entityIt == clientNetworkIdToEntity_.end()) continue; // no local ECS entity yet for this remote id -- real, honest "not spawned client-side yet"
        if (!interpolator.hasAnySnapshot()) continue;
        EntityState sampled = interpolator.sample(clockSeconds_);
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entityIt->second)) {
            transform->position = sampled.position;
            transform->rotation = sampled.rotation;
        }
    }

    (void)dt;
}

void NetworkSession::requestTeleport(uint32_t padNetworkId) {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::TeleportRequest));
    writer.writeU32(padNetworkId);
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::sendChatMessage(const std::string& text) {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::ChatMessage));
    writer.writeString(text);
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::sendDirectMessage(PlayerId recipient, const std::string& text) {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::DirectMessageSend));
    writer.writeU32(recipient);
    writer.writeString(text);
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::reportPlayer(PlayerId reported, moderation::ReportCategory category, const std::string& description) {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::ReportPlayer));
    writer.writeU32(reported);
    writer.writeU8(static_cast<uint8_t>(category));
    writer.writeString(description);
    networkStats_.recordPacketSent(writer.size());
    // Reliable: a player's report should never silently vanish to
    // unreliable-channel packet loss the way a per-tick position update
    // legitimately can (a stale one is worthless once a newer one
    // arrives; a lost report is just gone).
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::submitAppeal(const std::string& playerStatement, const std::string& relatedReviewCaseReason) {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::SubmitAppeal));
    writer.writeString(playerStatement);
    writer.writeString(relatedReviewCaseReason);
    networkStats_.recordPacketSent(writer.size());
    // Reliable, same real reasoning as reportPlayer()'s own comment: a
    // player's appeal should never silently vanish to unreliable-channel
    // packet loss.
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::selectTntWarsClass(tntwars::PlayerClassType classType) {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::SelectClass));
    writer.writeU8(static_cast<uint8_t>(classType));
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::fireTntWarsWeapon(glm::vec3 origin, glm::vec3 aimDirection) {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::FireWeapon));
    writer.writeVec3(origin);
    writer.writeVec3(aimDirection);
    networkStats_.recordPacketSent(writer.size());
    // Reliable: an accepted shot triggers a real, one-time broadcast
    // (ProjectileSpawned) every client deterministically replays from --
    // losing the real request itself (not just one of many per-tick
    // updates) would mean that shot simply never happened for anyone.
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::triggerTntWarsUltimate() {
    if (config_.mode != NetworkMode::Client) return;
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(WireMessageType::TriggerUltimate));
    networkStats_.recordPacketSent(writer.size());
    transport_.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kReliableChannel, true);
}

void NetworkSession::startStressTest(size_t syntheticPlayerCount, float inputsPerSecond) {
    if (config_.mode != NetworkMode::Server) return; // real, honest no-op -- see header comment
    stopStressTest();
    stressInputsPerSecond_ = inputsPerSecond;
    for (size_t i = 0; i < syntheticPlayerCount; ++i) {
        auto client = std::make_unique<StressClient>();
        if (client->transport.connectToServer("127.0.0.1", config_.port)) {
            stressClients_.push_back(std::move(client));
        }
    }
    core::logInfo("Network", "stress test started -- %zu/%zu real synthetic clients connected", stressClients_.size(),
                  syntheticPlayerCount);
}

void NetworkSession::stopStressTest() {
    for (auto& client : stressClients_) client->transport.shutdown();
    stressClients_.clear();
    stressInputsPerSecond_ = 0.0f;
}

void NetworkSession::tickStressTest(float dt) {
    if (stressClients_.empty()) return;
    float intervalSeconds = stressInputsPerSecond_ > 0.0f ? 1.0f / stressInputsPerSecond_ : 1.0f;

    for (auto& client : stressClients_) {
        ENetTransport::Callbacks noopCallbacks; // real synthetic clients don't act on server replies, just generate load
        client->transport.poll(0, noopCallbacks);

        client->inputAccumulatorSeconds += dt;
        while (client->inputAccumulatorSeconds >= intervalSeconds) {
            client->inputAccumulatorSeconds -= intervalSeconds;
            InputCommand command;
            command.sequence = client->nextSequence++;
            command.deltaTime = intervalSeconds;
            // Real, if synthetic, randomized movement intent -- a
            // genuine varied load, not the same static packet replayed.
            // Raw x/z are each independently in [-1, 1], so the vector can
            // reach length sqrt(2) before normalizing -- a real client's
            // WASD-derived moveAxis is already unit-length-or-less (see
            // InputCommand::moveAxis's contract), and isMovementPlausible()
            // enforces that server-side, so an un-normalized synthetic
            // vector here would just make the server correctly reject a
            // chunk of this stress test's own synthetic load.
            glm::vec3 rawAxis(static_cast<float>(std::rand() % 200 - 100) / 100.0f, 0.0f,
                               static_cast<float>(std::rand() % 200 - 100) / 100.0f);
            float rawAxisLength = glm::length(rawAxis);
            command.moveAxis = rawAxisLength > 1.0f ? rawAxis / rawAxisLength : rawAxis;
            command.yaw = static_cast<float>(std::rand() % 360);

            ByteWriter writer;
            writer.writeU8(static_cast<uint8_t>(WireMessageType::Input));
            serializeInputCommand(command, writer);
            client->transport.send(ENetTransport::kBroadcast, writer.bytes().data(), writer.size(), kUnreliableChannel,
                                    false);
        }
    }
}

} // namespace engine::net
