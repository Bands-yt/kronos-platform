#pragma once

#include <functional>
#include <string>
#include <vector>

namespace engine::studio {

// A generic command-pattern undo/redo stack -- each entry is a pair of
// closures capturing whatever state they need to reverse/reapply one
// action. Fixed-depth (see kMaxDepth) so a long editing session doesn't
// grow this unboundedly; the oldest entry is silently dropped when full,
// not an error (matches how every real editor's undo history works).
//
// Scope, stated plainly: this class is fully generic (any panel/plugin
// can push a Command), but only InspectorPanel's Transform fields are
// wired to push commands in this pass -- see its draw()'s
// IsItemActivated()/IsItemDeactivatedAfterEdit() usage. Extending
// coverage to other panels (Material Editor sliders, Align operations,
// entity creation/deletion) is the same pattern applied at each call
// site, not a redesign of this class.
class UndoStack {
public:
    struct Command {
        std::string label; // not surfaced in UI yet (no "Undo <label>" menu item), but real and populated from day one
        std::function<void()> undo;
        std::function<void()> redo;
    };

    static constexpr size_t kMaxDepth = 100;

    void push(Command command) {
        undoStack_.push_back(std::move(command));
        if (undoStack_.size() > kMaxDepth) {
            undoStack_.erase(undoStack_.begin());
        }
        redoStack_.clear(); // a new action invalidates redo history -- standard undo/redo semantics
    }

    void undo() {
        if (undoStack_.empty()) return;
        Command command = std::move(undoStack_.back());
        undoStack_.pop_back();
        command.undo();
        redoStack_.push_back(std::move(command));
    }

    void redo() {
        if (redoStack_.empty()) return;
        Command command = std::move(redoStack_.back());
        redoStack_.pop_back();
        command.redo();
        undoStack_.push_back(std::move(command));
    }

    [[nodiscard]] bool canUndo() const { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redoStack_.empty(); }

    // push() increases this, undo() decreases it, redo() increases it
    // again -- so "did undoCount() change at all since last frame" is a
    // real, cheap "the scene state was just modified" signal (in either
    // direction). Added for studio::SceneManager's autosave
    // dirty-tracking (see its header comment) rather than anything
    // internal to this class needing it.
    [[nodiscard]] size_t undoCount() const { return undoStack_.size(); }

private:
    std::vector<Command> undoStack_;
    std::vector<Command> redoStack_;
};

} // namespace engine::studio
