#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::core {

class ECS;
class Physics;

// Python game scripts on PocketPy. Each script gets its own VM with a memory limit, a time
// limit per call, and no files, OS, imports from disk or raw memory. See docs/PYTHON_SCRIPTS.md.
class PythonScripting {
public:
    using ScriptId = uint32_t;
    static constexpr ScriptId kInvalidScript = 0;
    // PocketPy has 16 VMs per process; slot 0 is its default VM, which Kronos leaves unused.
    static constexpr int kMaxScripts = 15;

    PythonScripting() = default;
    ~PythonScripting();
    PythonScripting(const PythonScripting&) = delete;
    PythonScripting& operator=(const PythonScripting&) = delete;

    void setWorld(ECS* ecs, Physics* physics) { ecs_ = ecs; physics_ = physics; }
    void setOutputCallback(std::function<void(const std::string&)> callback) { output_ = std::move(callback); }
    void setMaxMemoryBytesPerScript(size_t bytes) { maxMemoryBytes_ = bytes; }
    [[nodiscard]] size_t maxMemoryBytesPerScript() const { return maxMemoryBytes_; }
    void setMaxMillisPerTick(double ms) { maxMillisPerTick_ = ms; }
    void setMaxMillisForLoad(double ms) { maxMillisForLoad_ = ms; }

    // Runs the script's top level. Returns kInvalidScript (and reports why) if it fails or no VM is free.
    ScriptId load(const std::string& chunkName, const std::string& source);
    void unload(ScriptId id);
    void unloadAll();

    void tick(float dt);
    void fireCollision(uint32_t entityA, uint32_t entityB);

    [[nodiscard]] bool isRunning(ScriptId id) const;
    [[nodiscard]] size_t memoryUsed(ScriptId id) const;
    [[nodiscard]] size_t scriptCount() const { return scripts_.size(); }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }
    [[nodiscard]] double time() const { return clock_; }

    struct Bindings;

private:
    struct Script {
        ScriptId id = kInvalidScript;
        int slot = 0;
        std::string name;
        std::vector<std::string> reported;
    };

    enum class CallResult { Ok, Error, Stopped };

    Script* find(ScriptId id);
    const Script* find(ScriptId id) const;
    CallResult protectedCall(Script& script, bool (*fn)(void*), void* user, double millis);
    void fireHandlers(const char* list, int argc, const double* args, bool asInts, double millis);
    void stop(Script& script, const std::string& why);
    void release(Script& script);
    void report(Script& script, const std::string& message);
    void emit(const std::string& line);

    ECS* ecs_ = nullptr;
    Physics* physics_ = nullptr;
    std::function<void(const std::string&)> output_;
    size_t maxMemoryBytes_ = 64u * 1024u * 1024u;
    double maxMillisPerTick_ = 8.0;
    double maxMillisForLoad_ = 250.0;
    double clock_ = 0.0;
    ScriptId nextId_ = 1;
    std::vector<Script> scripts_;
    std::string lastError_;
};

} // namespace engine::core
