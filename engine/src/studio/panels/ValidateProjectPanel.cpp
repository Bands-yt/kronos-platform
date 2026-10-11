#include "studio/panels/ValidateProjectPanel.hpp"

#include <algorithm>
#include <cctype>

#include <imgui.h>

#include "core/Logger.hpp"

namespace engine::studio::panels {
namespace {

ImVec4 statusColor(migration::CheckStatus status) {
    switch (status) {
        case migration::CheckStatus::Pass: return {0.40f, 0.85f, 0.45f, 1.0f};
        case migration::CheckStatus::Fail: return {0.95f, 0.35f, 0.35f, 1.0f};
        case migration::CheckStatus::Warning:
        case migration::CheckStatus::Inconclusive: return {0.95f, 0.75f, 0.30f, 1.0f};
        default: return {0.60f, 0.60f, 0.65f, 1.0f};
    }
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

const char* kCategoryLabels[] = {"Blockers", "Gameplay risks", "Visual and audio", "Information"};

} // namespace

std::vector<std::string> ValidateProjectPanel::playErrorsSince(float mark) const {
    std::vector<std::string> out;
    for (const core::LogEntry& entry : core::Logger::instance().recentEntries(core::Logger::kMaxRingEntries)) {
        if (entry.timestampSeconds > mark && entry.level == core::LogLevel::Error &&
            migration::isScriptErrorLine(entry.message)) {
            out.push_back(entry.message);
        }
    }
    return out;
}

void ValidateProjectPanel::notePlayState(bool playing) {
    if (playing && !playing_) {
        const auto last = core::Logger::instance().recentEntries(1);
        playMark_ = last.empty() ? -1.0f : last.back().timestampSeconds;
        played_ = true;
    } else if (!playing && playing_) {
        lastPlayErrors_ = playErrorsSince(playMark_);
    }
    playing_ = playing;
}

void ValidateProjectPanel::run(core::ECS& ecs, const std::vector<std::string>& assetRoots) {
    migration::ValidationInput input;
    input.assetRoots = assetRoots;
    if (played_) input.lastPlayErrors = playing_ ? playErrorsSince(playMark_) : lastPlayErrors_;
    if (report_) previous_ = std::move(report_);
    report_ = migration::validateProject(ecs, input);
    added_.clear();
    resolved_ = 0;
    if (previous_) {
        const migration::ValidationDiff diff = migration::compareValidations(*previous_, *report_);
        added_ = diff.added;
        resolved_ = diff.resolved.size();
    }
    exportStatus_.clear();
}

std::optional<ValidateProjectPanel::Jump> ValidateProjectPanel::draw(core::ECS& ecs,
                                                                     const std::vector<std::string>& assetRoots) {
    if (!open_) return std::nullopt;
    if (runRequested_) {
        run(ecs, assetRoots);
        runRequested_ = false;
    }
    std::optional<Jump> jump;
    ImGui::SetNextWindowSize(ImVec2(720.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Validate Project", &open_)) {
        ImGui::End();
        return std::nullopt;
    }
    if (ImGui::Button(report_ ? "Run again" : "Run")) run(ecs, assetRoots);
    if (!report_) {
        ImGui::End();
        return std::nullopt;
    }
    const migration::ValidationReport& r = *report_;
    ImGui::SameLine();
    ImGui::TextColored(statusColor(r.overall()), "%s", migration::checkStatusName(r.overall()));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu objects, %zu scripts", r.instances, r.scripts);
    if (previous_) {
        ImGui::SameLine();
        ImGui::TextDisabled("| since last run: %zu new, %zu fixed", added_.size(), resolved_);
    }

    if (ImGui::BeginTable("checks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableSetupColumn("Detail");
        for (const migration::ProjectCheck& c : r.checks) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(statusColor(c.status), "%s", migration::checkStatusName(c.status));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.title.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", c.detail.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    const char* filterItems[] = {"All findings", "Blockers", "Gameplay risks", "Visual and audio", "Information"};
    int filterIndex = categoryFilter_ + 1;
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::Combo("##category", &filterIndex, filterItems, IM_ARRAYSIZE(filterItems))) categoryFilter_ = filterIndex - 1;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter by name or text", textFilter_, sizeof(textFilter_));

    const float exportHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("findings", ImVec2(0.0f, -exportHeight), ImGuiChildFlags_Borders);
    if (r.findings.empty()) ImGui::TextDisabled("Nothing to fix.");
    const std::string needle = lower(textFilter_);
    for (int c = 0; c <= 3; ++c) {
        if (categoryFilter_ >= 0 && categoryFilter_ != c) continue;
        std::vector<const migration::ProjectFinding*> rows;
        for (const migration::ProjectFinding& f : r.findings) {
            if (static_cast<int>(f.finding.category) != c) continue;
            if (!needle.empty() &&
                lower(f.finding.subject + " " + f.finding.message + " " + f.finding.code).find(needle) == std::string::npos) {
                continue;
            }
            rows.push_back(&f);
        }
        if (rows.empty()) continue;
        const std::string header = std::string(kCategoryLabels[c]) + " (" + std::to_string(rows.size()) + ")";
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
        for (const migration::ProjectFinding* p : rows) {
            const migration::DoctorFinding& f = p->finding;
            ImGui::PushID(p);
            const bool isNew = std::find(added_.begin(), added_.end(), p->stableId()) != added_.end();
            std::string label = (isNew ? "[new] " : "") + f.subject;
            if (f.line > 0) label += "  line " + std::to_string(f.line);
            if (f.count > 1) label += "  x" + std::to_string(f.count);
            if (ImGui::Selectable(label.c_str(), false) && p->entity != ~0u) {
                const auto entity = static_cast<core::EntityId>(p->entity);
                if (ecs.raw().valid(entity)) jump = Jump{entity, f.line};
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s\nWhy: %s\nFix: %s%s", f.message.c_str(), f.why.c_str(), f.help.c_str(),
                                  p->entity != ~0u ? "\nClick to select it." : "");
            }
            ImGui::Indent();
            ImGui::TextDisabled("%s", f.message.c_str());
            ImGui::Unindent();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(360.0f);
    ImGui::InputText("##exportPath", exportPath_, sizeof(exportPath_));
    ImGui::SameLine();
    const bool md = ImGui::Button("Export Markdown");
    ImGui::SameLine();
    const bool json = ImGui::Button("Export JSON");
    if (md || json) {
        std::string error;
        const std::string path = exportPath_;
        exportStatus_ = migration::writeDoctorFile(path, md ? migration::validationMarkdown(r) : migration::validationJson(r),
                                                   error)
                            ? "Saved " + path
                            : "Couldn't save: " + error;
    }
    if (!exportStatus_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", exportStatus_.c_str());
    }
    ImGui::End();
    return jump;
}

} // namespace engine::studio::panels
