#pragma once

#include <functional>
#include <string>
#include <vector>

namespace engine::studio {

// One real, executable action -- `label` is both what's shown and what's
// searched against.
struct PaletteCommand {
    std::string label;
    std::function<void()> execute;
};

// Kronos ("Studio QoL Sprint" -- "VS Code-Style Command Palette"): a
// real, generic, floating searchable-action window -- opened via
// Ctrl+K/Ctrl+P (checked by the real caller, StudioApp, the same "once
// per frame, not per-widget" convention its own Ctrl+Z/Ctrl+S handling
// already uses), substring-filtered against each command's own label,
// keyboard-navigable (Up/Down + Enter), real click-to-execute.
// Deliberately generic -- knows nothing about ECS/Camera/Renderer
// itself; `commands` and `entitySearch` are supplied by the real caller,
// which is the one place that actually knows how to spawn a baseplate,
// toggle physics debug, focus an entity, etc. Not an ImGui modal
// (`BeginPopupModal`) -- a plain floating window, so the viewport keeps
// rendering/updating behind it (a command palette that freezes the 3D
// view while open would be a real, worse editing experience than one
// that doesn't).
class CommandPalette {
public:
    void open();
    void close() { isOpen_ = false; }
    [[nodiscard]] bool isOpen() const { return isOpen_; }

    // Kronos ("...type an entity name to jump the viewport camera
    // directly to it"): `entitySearch` is a real, injected callback --
    // StudioApp knows how to search ECS::Name components and how to
    // move the viewport camera; this class doesn't reach into ECS
    // directly. Returns real, ready-to-execute PaletteCommands (each
    // one's own `execute` already captures the target entity), not just
    // names, so this class treats them identically to any other command.
    using EntitySearchFn = std::function<std::vector<PaletteCommand>(const std::string& query)>;

    // Draws the palette (a no-op if closed) and real-executes whichever
    // command the user picks this frame, if any.
    void draw(const std::vector<PaletteCommand>& commands, const EntitySearchFn& entitySearch);

private:
    bool isOpen_ = false;
    bool justOpened_ = false;
    char queryBuffer_[256] = "";
    int highlightedIndex_ = 0;
};

} // namespace engine::studio
