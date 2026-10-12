#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "studio/RustPluginProject.hpp"

namespace engine::studio::panels {

class ColorTextEditBackend;

// Studio's "Rust Plugins" window: make a plugin from the template, edit lib.rs, build it with
// cargo and install it into native_plugins/ (Studio reloads trusted plugins by itself).
class RustPluginPanel {
public:
    struct Paths {
        std::filesystem::path projects;  // one folder per plugin
        std::filesystem::path templateDir;
        std::filesystem::path sdkDir;
        std::filesystem::path pluginDir; // where installed libraries go
    };

    RustPluginPanel();
    ~RustPluginPanel();

    void open() { open_ = true; }
    [[nodiscard]] bool isOpen() const { return open_; }
    void draw(const Paths& paths);

private:
    void refresh(const Paths& paths);
    void select(size_t index);
    bool save();
    void startBuild(bool install);

    bool open_ = false;
    bool listed_ = false;
    int cargo_ = -1; // -1 unknown, 0 no, 1 yes
    char newName_[64] = "";
    std::string status_;
    std::vector<RustProjectInfo> projects_;
    int selected_ = -1;
    std::unique_ptr<ColorTextEditBackend> editor_;
    std::string savedText_;
    RustBuild build_;
    bool installAfterBuild_ = false;
    std::filesystem::path installTo_;
};

} // namespace engine::studio::panels
