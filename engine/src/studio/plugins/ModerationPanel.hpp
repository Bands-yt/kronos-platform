#pragma once

#include "net/NetworkSession.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Sprint 12 ("Moderation & Safety Systems") task 5's real Studio-side
// moderation tools -- world-level safety configuration, a live chat log
// viewer, a real "Report Player" submission form + report log, the real
// human-review queue safety::TrustSafetyService's escalation callbacks
// feed (see net::NetworkSession's own header comment on how those
// callbacks finally got wired to something real this sprint), a manual
// server-mute control, and trusted-creator management.
//
// Same "plugin holds a reference to the NetworkSession StudioApp already
// owns" pattern plugins::NetworkOverlayPlugin already established (see
// that plugin's own class comment) -- this is a separate panel, not a
// second section bolted onto NetworkOverlayPlugin, since that plugin is
// scoped to connection/stress-test debug tooling while this one is
// scoped to moderation/safety configuration and review; sharing the one
// underlying NetworkSession reference doesn't mean sharing UI surface.
class ModerationPanel final : public IStudioPlugin {
public:
    explicit ModerationPanel(net::NetworkSession& session) : session_(&session) {}

    [[nodiscard]] const char* name() const override { return "Moderation"; }
    [[nodiscard]] const char* category() const override { return "Diagnostics"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawWorldSafetySection();
    void drawChatLogSection();
    void drawReportSection();
    void drawReviewQueueSection();
    void drawTrustedCreatorSection();
    void drawServerMuteSection();

    net::NetworkSession* session_;

    // Report Player form state.
    int reportTargetId_ = 0;
    int reportCategoryIndex_ = 0;
    char reportDescriptionBuffer_[256] = "";
    std::string reportStatus_;

    // Trusted Creator form state.
    int trustTargetId_ = 0;

    // Server Mute form state.
    int muteTargetId_ = 0;
};

} // namespace engine::studio::plugins
