#include "studio/plugins/NetworkOverlayPlugin.hpp"

#include <chrono>
#include <cstdio>

#include <imgui.h>

#include "core/Components.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
constexpr const char* kSessionHistoryPath = "session_history.sessions";
constexpr const char* kLocalProfilePath = "local_profile.profile";
} // namespace

NetworkOverlayPlugin::NetworkOverlayPlugin(net::NetworkSession& session) : session_(&session) {}

void NetworkOverlayPlugin::update(float dt, core::ECS& ecs, core::EntityId, const std::vector<core::EntityId>&) {
    // Real, lazy load -- deferred out of the constructor so a plugin that
    // never opens its window (and never connects to anything) never even
    // touches the filesystem, same "real work only when it's actually
    // needed" shape core::ScriptedPlugin's own lazy trailerScriptApi_
    // construction already established.
    if (!sessionHistoryLoaded_) {
        (void)sessionHistory_.loadFromFile(kSessionHistoryPath); // a real, honest no-op if the file doesn't exist yet
        sessionHistoryLoaded_ = true;
    }
    if (!localProfileLoaded_) {
        localProfile_ = core::loadOrCreateProfile(kLocalProfilePath); // real create-on-first-use, same as sessionHistory_
        std::snprintf(displayNameBuffer_, sizeof(displayNameBuffer_), "%s", localProfile_.displayName.c_str());
        localProfileLoaded_ = true;
    }

    // A real, honest no-op while Offline (net::NetworkSession::tick()'s
    // own contract) -- this runs every frame regardless of whether the
    // panel is open, matching every other background-work plugin.
    session_->tick(dt, ecs, monitorEntity_);

    // Kronos (Alpha Roadmap Phase 9, "Platform Services" -- "Multiplayer
    // session browser"): real-record a successful client connection
    // exactly once per real session, the moment the real handshake
    // actually completes (localPlayerId() stops being kInvalidPlayer) --
    // not at the optimistic "Join Server" button click, which only means
    // a connection attempt started, not that it succeeded.
    if (session_->isClient() && session_->localPlayerId() != net::kInvalidPlayer && !recordedThisConnection_) {
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        sessionHistory_.recordConnection(serverLabelBuffer_, addressBuffer_, static_cast<uint16_t>(portValue_), now);
        (void)sessionHistory_.saveToFile(kSessionHistoryPath);
        recordedThisConnection_ = true;
    }
}

void NetworkOverlayPlugin::stopSession() {
    session_->shutdown();
    monitorEntity_ = core::kNullEntity;
    recordedThisConnection_ = false;
    statusText_ = "Disconnected.";
}

void NetworkOverlayPlugin::drawRecentServersSection() {
    if (session_->isActive() || modeIndex_ != 1) return; // only meaningful while picking a server to Join
    if (sessionHistory_.size() == 0) return;

    if (!ImGui::CollapsingHeader("Recent Servers")) return;
    ImGui::TextDisabled("A real local history of servers you've connected to -- not live network discovery.");
    for (const auto& entry : sessionHistory_.entriesMostRecentFirst()) {
        ImGui::PushID(entry.address.c_str());
        ImGui::PushID(entry.port);
        if (entry.label.empty()) {
            ImGui::Text("%s:%u", entry.address.c_str(), entry.port);
        } else {
            ImGui::Text("%s (%s:%u)", entry.label.c_str(), entry.address.c_str(), entry.port);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Use")) {
            std::snprintf(addressBuffer_, sizeof(addressBuffer_), "%s", entry.address.c_str());
            portValue_ = entry.port;
            std::snprintf(serverLabelBuffer_, sizeof(serverLabelBuffer_), "%s", entry.label.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            sessionHistory_.removeEntry(entry.address, entry.port);
            (void)sessionHistory_.saveToFile(kSessionHistoryPath);
        }
        ImGui::PopID();
        ImGui::PopID();
    }
}

void NetworkOverlayPlugin::drawConnectionSection(core::ECS& ecs) {
    if (!ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool active = session_->isActive();
    // Real local-profile integration: editable while not connected (a
    // display name change mid-session doesn't retroactively resend --
    // simplest honest scope, matching Mode/Port/Address's own
    // BeginDisabled(active) below).
    ImGui::BeginDisabled(active);
    if (ImGui::InputText("Playing As", displayNameBuffer_, sizeof(displayNameBuffer_))) {
        localProfile_.displayName = displayNameBuffer_;
        (void)localProfile_.saveToFile(kLocalProfilePath);
    }
    const char* modes[] = {"Host (Server)", "Join (Client)"};
    ImGui::Combo("Mode", &modeIndex_, modes, 2);
    ImGui::InputInt("Port", &portValue_);
    portValue_ = portValue_ < 1 ? 1 : (portValue_ > 65535 ? 65535 : portValue_);
    if (modeIndex_ == 1) {
        ImGui::InputText("Address", addressBuffer_, sizeof(addressBuffer_));
        ImGui::InputText("Server Name (optional)", serverLabelBuffer_, sizeof(serverLabelBuffer_));
    }
    ImGui::EndDisabled();

    if (!active) {
        if (ImGui::Button(modeIndex_ == 0 ? "Host Server" : "Join Server")) {
            net::NetworkSession::Config config;
            config.mode = modeIndex_ == 0 ? net::NetworkMode::Server : net::NetworkMode::Client;
            config.port = static_cast<uint16_t>(portValue_);
            config.serverAddress = addressBuffer_;

            // Kronos ("Active Joining UI" -- NetworkSession protocol
            // foundation): the real display name now travels WITH the
            // client's own real JoinRequest handshake, set here before
            // initialize() -- a real, honest no-op in Server mode. This
            // supersedes the separate post-connect "SetDisplayName"
            // RemoteEvent this plugin used to fire/handle itself (the
            // server now sets the avatar's real Name directly from the
            // join handshake, see NetworkSession::handleJoinRequestServer()).
            session_->setLocalDisplayName(localProfile_.displayName);

            if (session_->initialize(config)) {
                if (config.mode == net::NetworkMode::Server) {
                    // Same real "spawn a minimal Transform+Name avatar per
                    // joining player" gameplay hook core::Application's
                    // startNetworking() wires for engine_runtime -- reused
                    // here rather than defined a second time, since
                    // NetworkSession's onPlayerJoin_ callback is exactly
                    // the same seam either owner injects it through.
                    session_->setOnPlayerJoin([](core::ECS& joinEcs, net::PlayerId player) -> core::EntityId {
                        std::string entityName = "NetworkPlayer" + std::to_string(player);
                        core::EntityId entity = joinEcs.createEntity(entityName);
                        std::fprintf(stdout, "NetworkOverlayPlugin: spawned real avatar entity for player %u\n", player);
                        return entity;
                    });
                    statusText_ = "Hosting on port " + std::to_string(portValue_) + ".";
                } else {
                    // Real, inert monitor entity -- see class comment on
                    // why this must be a real, non-null EntityId rather
                    // than kNullEntity.
                    monitorEntity_ = ecs.createEntity("NetworkMonitor");
                    ecs.addComponent<core::Transform>(monitorEntity_, core::Transform{});
                    statusText_ = "Connecting to " + std::string(addressBuffer_) + ":" + std::to_string(portValue_) + "...";
                }
            } else {
                statusText_ = "Failed to start networking.";
                session_->shutdown();
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.35f, 0.80f, 0.40f, 1.0f), "%s", session_->isServer() ? "Hosting" : "Connected (client)");
        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) stopSession();

        if (session_->isClient()) {
            ImGui::Text("Local player id: %u", session_->localPlayerId());
        } else {
            ImGui::Text("Connected peers: %zu", session_->connectedPeerCount());
        }
    }

    if (!statusText_.empty()) ImGui::TextDisabled("%s", statusText_.c_str());
}

void NetworkOverlayPlugin::drawConnectedPlayersSection(core::ECS& ecs) {
    if (!session_->isServer()) return; // real "who's here" list only makes sense host-side
    if (!ImGui::CollapsingHeader("Connected Players")) return;

    std::vector<net::PlayerId> playerIds = session_->connectedPlayerIds();
    if (playerIds.empty()) {
        ImGui::TextDisabled("No players connected.");
        return;
    }
    // Kronos (Alpha Completion Checklist, "Multiplayer Session UX" --
    // "join/leave flow"): the real, creator-facing name each player's
    // client sent in its own real JoinRequest handshake (see
    // NetworkSession::handleJoinRequestServer()), not just a bare numeric
    // PlayerId -- makes "who's here" actually legible to a host, the same
    // real gap local-profile integration closed.
    for (net::PlayerId player : playerIds) {
        core::EntityId entity = session_->playerEntity(player);
        const auto* nameComponent = entity != core::kNullEntity ? ecs.tryGetComponent<core::Name>(entity) : nullptr;
        std::string displayName = (nameComponent != nullptr && !nameComponent->value.empty())
                                       ? nameComponent->value
                                       : ("NetworkPlayer" + std::to_string(player));
        ImGui::Text("#%u  %s%s", player, displayName.c_str(), session_->isServerMuted(player) ? " (muted)" : "");
    }
}

void NetworkOverlayPlugin::drawStatsSection() {
    if (!ImGui::CollapsingHeader("Live Stats", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!session_->isActive()) {
        ImGui::TextDisabled("Not connected.");
        return;
    }

    net::NetworkStats& stats = session_->stats();
    ImGui::Text("Packets sent: %llu (%.1f/s)", static_cast<unsigned long long>(stats.totalPacketsSent()),
                static_cast<double>(stats.packetsSentPerSecond()));
    ImGui::Text("Packets received: %llu (%.1f/s)", static_cast<unsigned long long>(stats.totalPacketsReceived()),
                static_cast<double>(stats.packetsReceivedPerSecond()));
    ImGui::Text("Packets dropped: %llu", static_cast<unsigned long long>(stats.totalPacketsDropped()));
    ImGui::Text("Bytes sent / received: %llu / %llu", static_cast<unsigned long long>(stats.totalBytesSent()),
                static_cast<unsigned long long>(stats.totalBytesReceived()));

    if (session_->isClient() && session_->localPlayerId() != net::kInvalidPlayer) {
        float ping = stats.averagePingMs(session_->localPlayerId());
        if (ping > 0.0f) {
            ImGui::Text("Ping: %.0f ms", static_cast<double>(ping));
        } else {
            ImGui::TextDisabled("Ping: -- (no samples yet)");
        }
    }
}

void NetworkOverlayPlugin::drawStressTestSection() {
    if (!ImGui::CollapsingHeader("Network Stress Test")) return;
    if (!session_->isServer()) {
        ImGui::TextDisabled("Host a server to run a stress test.");
        return;
    }

    ImGui::BeginDisabled(session_->isStressTestRunning());
    ImGui::SliderInt("Synthetic players", &stressPlayerCount_, 1, 100);
    ImGui::SliderFloat("Inputs/sec per player", &stressRate_, 1.0f, 60.0f);
    ImGui::EndDisabled();

    if (!session_->isStressTestRunning()) {
        if (ImGui::Button("Start Stress Test")) {
            session_->startStressTest(static_cast<size_t>(stressPlayerCount_), stressRate_);
        }
    } else {
        ImGui::Text("Running: %zu real synthetic clients connected", session_->stressTestClientCount());
        if (ImGui::Button("Stop Stress Test")) session_->stopStressTest();
    }
}

void NetworkOverlayPlugin::drawPanel(core::ECS& ecs, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Network Overlay");

    drawConnectionSection(ecs);
    drawRecentServersSection();
    drawConnectedPlayersSection(ecs);
    drawStatsSection();
    drawStressTestSection();

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
