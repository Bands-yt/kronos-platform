#pragma once

#include <string>

#include "core/ECS.hpp"
#include "core/LocalProfile.hpp"
#include "net/NetworkSession.hpp"
#include "net/SessionHistory.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Sprint 11 ("Networking Foundation") task 4: the Studio-side real
// network debug tool -- shows real, live net::NetworkStats (ping,
// packet rate, dropped packets) and drives the real "Network Stress
// Test" mode (net::NetworkSession::startStressTest()), the Studio
// counterpart to engine_runtime's `--server --stress N` CLI flags (see
// main.cpp's own comment on that entry point).
//
// Owns no NetworkSession itself -- StudioApp does (constructed alongside
// its ecs_/renderer_, same "plugin holds a reference to something
// StudioApp already owns" pattern DiagnosticsPlugin's Profiler&/
// PerformanceMetrics& already established) -- this plugin is purely the
// UI + the real Host/Join/Disconnect control flow over it.
//
// Deliberately Server-hosting and passive-Client-monitoring only, not a
// playable networked session: Studio's own ECS is a static scene-editing
// surface (see StudioApp's class comment -- "no Physics here... not
// simulated bodies"), so Join mode spawns a minimal, real but inert
// Transform-only "monitor" entity (never rendered, never moved by local
// input) purely so NetworkSession::tick()'s client-side reconciliation
// path has a real, non-null EntityId to run against -- the same
// kNullEntity-must-never-reach-ecs.tryGetComponent() convention
// core::CharacterController::tick() already established is honored here
// by never handing the session a null entity in Client mode, not by
// tick() happening to tolerate one.
class NetworkOverlayPlugin final : public IStudioPlugin {
public:
    explicit NetworkOverlayPlugin(net::NetworkSession& session);

    [[nodiscard]] const char* name() const override { return "Network Overlay"; }
    [[nodiscard]] const char* category() const override { return "Diagnostics"; }

    // Real background work regardless of window visibility (see
    // IStudioPlugin's own class comment) -- this is what actually pumps
    // the real ENet transport/stress-test clients every frame; a closed
    // window must not silently pause a live session.
    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawConnectionSection(core::ECS& ecs);
    // Kronos (Alpha Roadmap Phase 9, "Platform Services" -- "Multiplayer
    // session browser"): real "Recent Servers" list -- see
    // net/SessionHistory.hpp's own header comment on why this is a real,
    // local connection history rather than real network discovery.
    void drawRecentServersSection();
    // Kronos (Alpha Completion Checklist, "Multiplayer Session UX" --
    // "join/leave flow"): a real, host-only, name-legible "who's
    // connected right now" list -- see net::NetworkSession::
    // connectedPlayerIds()'s own comment for why connectedPeerCount()'s
    // bare count wasn't enough on its own.
    void drawConnectedPlayersSection(core::ECS& ecs);
    void drawStatsSection();
    void drawStressTestSection();
    void stopSession();

    net::NetworkSession* session_;
    net::SessionHistory sessionHistory_;
    bool sessionHistoryLoaded_ = false;

    // Kronos (Alpha Completion Checklist, "Multiplayer Session UX" --
    // "local profile integration"): core::LocalProfile (Alpha Roadmap
    // Phase 9) existed but was never wired into any UI or handshake --
    // this is that real, honest follow-up. Lazily loaded the same way
    // sessionHistory_ is, so a plugin that never opens its window never
    // touches the filesystem for it either.
    core::LocalProfile localProfile_;
    bool localProfileLoaded_ = false;
    char displayNameBuffer_[64] = "";
    // Optional creator-facing label for the server being joined -- fixes a
    // real, visible gap where drawRecentServersSection() always rendered
    // a blank label (recordConnection() was previously always called with
    // "").
    char serverLabelBuffer_[64] = "";

    int modeIndex_ = 0; // 0 = Host (Server), 1 = Join (Client)
    char addressBuffer_[128] = "127.0.0.1";
    int portValue_ = 7777;
    int stressPlayerCount_ = 10;
    float stressRate_ = 20.0f;

    // Client-mode-only real inert entity -- see class comment. kNullEntity
    // (the default) means "not connected as a client right now".
    core::EntityId monitorEntity_ = core::kNullEntity;
    // Real, honest "have we already recorded this specific successful
    // connection" guard -- update() checks localPlayerId() every frame
    // while connecting, but a real connection should only ever be
    // recorded into sessionHistory_ once per real session, not once per
    // frame it happens to still be connected.
    bool recordedThisConnection_ = false;

    std::string statusText_;
};

} // namespace engine::studio::plugins
