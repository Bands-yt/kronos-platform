#include "studio/Notification.hpp"

#include <algorithm>

#include <imgui.h>

namespace engine::studio {

void NotificationCenter::push(std::string message, NotificationSeverity severity, float durationSeconds) {
    if (notifications_.size() >= kMaxNotifications) {
        // Real, honest overflow behavior -- drop the single oldest
        // (front of the FIFO) rather than silently refusing the new one
        // or growing unbounded. A burst of rapid saves/errors degrades
        // gracefully instead of covering the whole screen.
        notifications_.erase(notifications_.begin());
    }
    notifications_.push_back(Notification{std::move(message), severity, durationSeconds, durationSeconds});
}

void NotificationCenter::tick(float dt) {
    for (Notification& notification : notifications_) notification.remainingSeconds -= dt;
    notifications_.erase(std::remove_if(notifications_.begin(), notifications_.end(),
                                         [](const Notification& n) { return n.remainingSeconds <= 0.0f; }),
                          notifications_.end());
}

namespace {
ImVec4 severityColor(NotificationSeverity severity) {
    // Real, semantic severity colors -- deliberately separate from
    // StudioStyle's own teal/cyan accent (see that file's header
    // comment): a notification's color communicates *what happened*
    // (good/warning/bad), not "this is an interactive Studio element",
    // so it shouldn't share the accent's hue.
    switch (severity) {
        case NotificationSeverity::Info: return ImVec4(0.45f, 0.55f, 0.70f, 1.0f);
        case NotificationSeverity::Success: return ImVec4(0.35f, 0.72f, 0.42f, 1.0f);
        case NotificationSeverity::Warning: return ImVec4(0.85f, 0.65f, 0.20f, 1.0f);
        case NotificationSeverity::Error: return ImVec4(0.85f, 0.30f, 0.30f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

const char* severityLabel(NotificationSeverity severity) {
    switch (severity) {
        case NotificationSeverity::Info: return "Info";
        case NotificationSeverity::Success: return "Success";
        case NotificationSeverity::Warning: return "Warning";
        case NotificationSeverity::Error: return "Error";
    }
    return "Info";
}
} // namespace

void NotificationCenter::draw() const {
    if (notifications_.empty()) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float kToastWidth = 320.0f;
    constexpr float kPadding = 12.0f;
    float y = viewport->WorkPos.y + kPadding;

    for (const Notification& notification : notifications_) {
        // Fade in/out over the real last 25% of its lifetime -- a flat
        // "pop then vanish" reads worse than a real, if simple, fade.
        float lifeFraction = notification.totalSeconds > 0.0f ? notification.remainingSeconds / notification.totalSeconds : 0.0f;
        float alpha = std::clamp(lifeFraction / 0.25f, 0.0f, 1.0f);

        ImVec2 pos(viewport->WorkPos.x + viewport->WorkSize.x - kToastWidth - kPadding, y);
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(kToastWidth, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.92f * alpha);

        ImVec4 color = severityColor(notification.severity);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(color.x, color.y, color.z, alpha));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, alpha));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

        char windowId[32];
        std::snprintf(windowId, sizeof(windowId), "##toast_%p", static_cast<const void*>(&notification));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
        ImGui::Begin(windowId, nullptr, flags);
        ImGui::TextColored(ImVec4(color.x, color.y, color.z, alpha), "%s", severityLabel(notification.severity));
        ImGui::TextWrapped("%s", notification.message.c_str());
        ImVec2 windowSize = ImGui::GetWindowSize();
        ImGui::End();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        y += windowSize.y + 8.0f;
    }
}

} // namespace engine::studio
