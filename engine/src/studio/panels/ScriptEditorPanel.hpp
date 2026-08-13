#pragma once

#include <memory>
#include <string>

namespace engine::studio::panels {

// docs/ARCHITECTURE.md §5/§9's Script Editor panel: "Monaco + Luau's own
// analysis toolchain for real autocomplete/type-checking." Real Monaco
// integration needs an embedded webview (CEF or Ultralight, per §5's
// Studio-shell table) -- a dependency well outside this skeleton's scope.
// IScriptEditorBackend is the seam that integration attaches to later:
// ScriptEditorPanel talks to this interface, never to ImGui widgets or a
// webview directly, so swapping the backend is a one-class change, not a
// panel rewrite.
class IScriptEditorBackend {
public:
    virtual ~IScriptEditorBackend() = default;

    [[nodiscard]] virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void setSource(const std::string& source) = 0;
    [[nodiscard]] virtual const std::string& source() const = 0;

    virtual void draw() = 0; // draws itself into the current ImGui window
    [[nodiscard]] virtual const char* backendName() const = 0;
};

// The only backend that actually works today: a plain ImGui multiline
// text box (via imgui's misc/cpp/imgui_stdlib helper). No syntax
// highlighting, no autocomplete, no Luau.Ast-backed analysis -- genuinely
// functional as a text editor, nothing more. This is what
// core::Scripting::loadAndRun() (see core/Scripting.hpp) can actually take
// a string from today.
class ImGuiFallbackEditor final : public IScriptEditorBackend {
public:
    [[nodiscard]] bool initialize() override { return true; }
    void shutdown() override {}

    void setSource(const std::string& source) override { buffer_ = source; }
    [[nodiscard]] const std::string& source() const override { return buffer_; }

    void draw() override;
    [[nodiscard]] const char* backendName() const override { return "ImGui text box (fallback)"; }

private:
    std::string buffer_;
};

// Not implemented -- see the class comment on IScriptEditorBackend.
// initialize() always returns false; ScriptEditorPanel falls back to
// ImGuiFallbackEditor when it does, so a Studio build never silently ends
// up with no editor at all.
class MonacoWebViewEditor final : public IScriptEditorBackend {
public:
    [[nodiscard]] bool initialize() override;
    void shutdown() override {}

    void setSource(const std::string& source) override { pendingSource_ = source; }
    [[nodiscard]] const std::string& source() const override { return pendingSource_; }

    void draw() override {}
    [[nodiscard]] const char* backendName() const override { return "Monaco (webview, not implemented)"; }

private:
    std::string pendingSource_;
};

class ScriptEditorPanel {
public:
    ScriptEditorPanel();

    void draw();

    void loadSource(const std::string& source) { backend_->setSource(source); }
    [[nodiscard]] const std::string& source() const { return backend_->source(); }

private:
    std::unique_ptr<IScriptEditorBackend> backend_;
};

} // namespace engine::studio::panels
