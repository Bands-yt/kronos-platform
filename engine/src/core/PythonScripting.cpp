#include "core/PythonScripting.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"
#include "core/Physics.hpp"

#include "kronos_pocketpy.h"
#include "pocketpy.h"

namespace engine::core {

namespace {

// PocketPy's VMs are process-wide, so every PythonScripting shares these.
struct SlotOwner {
    PythonScripting* host = nullptr;
    PythonScripting::ScriptId id = PythonScripting::kInvalidScript;
    std::string printLine;
};

std::recursive_mutex& pyMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

SlotOwner g_slots[KRONOS_PY_SLOTS];

constexpr size_t kMaxPrintLine = 4096;
constexpr size_t kMaxReported = 16;
constexpr double kUnloadMillis = 50.0;

struct ExecArgs {
    const char* source;
    const char* name;
};

struct CallArgs {
    py_TValue fn;
    int argc;
    py_TValue argv[2];
};

bool runExec(void* user) {
    auto* args = static_cast<ExecArgs*>(user);
    return py_exec(args->source, args->name, EXEC_MODE, nullptr);
}

bool runCall(void* user) {
    auto* args = static_cast<CallArgs*>(user);
    return py_call(&args->fn, args->argc, args->argv);
}

std::string megabytes(size_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

} // namespace

struct PythonScripting::Bindings {
    static PythonScripting* host() {
        int slot = py_currentvm();
        return slot > 0 && slot < KRONOS_PY_SLOTS ? g_slots[slot].host : nullptr;
    }

    static void flushPrint(SlotOwner& owner) {
        if (owner.host == nullptr) return;
        owner.host->emit(owner.printLine);
        owner.printLine.clear();
    }

    static void onPrint(const char* text) {
        int slot = py_currentvm();
        if (slot <= 0 || slot >= KRONOS_PY_SLOTS) return;
        SlotOwner& owner = g_slots[slot];
        for (const char* c = text; *c != '\0'; ++c) {
            if (*c == '\n') {
                flushPrint(owner);
            } else {
                owner.printLine.push_back(*c);
                if (owner.printLine.size() >= kMaxPrintLine) flushPrint(owner);
            }
        }
    }

    static void onFlush() {}
    static int noInput() { return -1; }
    static char* denyImport(const char*, int*) { return nullptr; }

    static bool exitBlocked(int, py_Ref) {
        return py_exception(tp_RuntimeError, "exit() is not available in game scripts");
    }
    static bool inputBlocked(int, py_Ref) {
        return py_exception(tp_RuntimeError, "input() is not available in game scripts");
    }
    static bool sleepBlocked(int, py_Ref) {
        return py_exception(tp_RuntimeError, "time.sleep() would freeze the game; use events.on_update instead");
    }

    static bool entityArg(py_Ref value, EntityId& out) {
        if (!py_checktype(value, tp_int)) return false;
        py_i64 raw = py_toint(value);
        if (raw < 0 || raw > 0xFFFFFFFFll) return py_exception(tp_ValueError, "not an entity id");
        out = static_cast<EntityId>(static_cast<uint32_t>(raw));
        return true;
    }

    static bool vec3Args(py_Ref argv, glm::vec3& out) {
        double v[3];
        for (int i = 0; i < 3; ++i) {
            if (!py_castfloat(&argv[i], &v[i])) return false;
            if (!std::isfinite(v[i])) return py_exception(tp_ValueError, "coordinates must be finite numbers");
        }
        out = glm::vec3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
        return true;
    }

    static ECS* world(EntityId entity) {
        PythonScripting* self = host();
        if (self == nullptr || self->ecs_ == nullptr) return nullptr;
        return self->ecs_->raw().valid(entity) ? self->ecs_ : nullptr;
    }

    static bool find(int argc, py_Ref argv) {
        PY_CHECK_ARGC(1);
        PY_CHECK_ARG_TYPE(0, tp_str);
        const char* name = py_tostr(py_arg(0));
        PythonScripting* self = host();
        py_newnone(py_retval());
        if (self == nullptr || self->ecs_ == nullptr) return true;
        uint32_t found = 0;
        bool hit = false;
        for (auto entity : self->ecs_->view<Name>()) {
            const auto* label = self->ecs_->tryGetComponent<Name>(entity);
            if (label != nullptr && label->value == name) {
                found = static_cast<uint32_t>(entity);
                hit = true;
                break;
            }
        }
        if (hit) py_newint(py_retval(), found);
        return true;
    }

    static bool name(int argc, py_Ref argv) {
        PY_CHECK_ARGC(1);
        EntityId entity;
        if (!entityArg(py_arg(0), entity)) return false;
        py_newnone(py_retval());
        if (ECS* ecs = world(entity)) {
            if (const auto* label = ecs->tryGetComponent<Name>(entity)) py_newstr(py_retval(), label->value.c_str());
        }
        return true;
    }

    static bool exists(int argc, py_Ref argv) {
        PY_CHECK_ARGC(1);
        EntityId entity;
        if (!entityArg(py_arg(0), entity)) return false;
        py_newbool(py_retval(), world(entity) != nullptr);
        return true;
    }

    static bool getPosition(int argc, py_Ref argv) {
        PY_CHECK_ARGC(1);
        EntityId entity;
        if (!entityArg(py_arg(0), entity)) return false;
        py_newnone(py_retval());
        ECS* ecs = world(entity);
        const Transform* transform = ecs ? ecs->tryGetComponent<Transform>(entity) : nullptr;
        if (transform == nullptr) return true;
        py_ObjectRef items = py_newtuple(py_retval(), 3);
        py_newfloat(&items[0], transform->position.x);
        py_newfloat(&items[1], transform->position.y);
        py_newfloat(&items[2], transform->position.z);
        return true;
    }

    static bool setPosition(int argc, py_Ref argv) {
        PY_CHECK_ARGC(4);
        EntityId entity;
        glm::vec3 position;
        if (!entityArg(py_arg(0), entity) || !vec3Args(py_arg(1), position)) return false;
        if (ECS* ecs = world(entity)) {
            if (auto* transform = ecs->tryGetComponent<Transform>(entity)) {
                transform->position = position;
                instances::markBodyMoved(*ecs, entity);
            }
        }
        py_newnone(py_retval());
        return true;
    }

    static bool applyImpulse(int argc, py_Ref argv) {
        PY_CHECK_ARGC(4);
        EntityId entity;
        glm::vec3 impulse;
        if (!entityArg(py_arg(0), entity) || !vec3Args(py_arg(1), impulse)) return false;
        PythonScripting* self = host();
        if (world(entity) != nullptr && self->physics_ != nullptr) self->physics_->applyImpulse(entity, *self->ecs_, impulse);
        py_newnone(py_retval());
        return true;
    }

    static bool setVelocity(int argc, py_Ref argv) {
        PY_CHECK_ARGC(4);
        EntityId entity;
        glm::vec3 velocity;
        if (!entityArg(py_arg(0), entity) || !vec3Args(py_arg(1), velocity)) return false;
        PythonScripting* self = host();
        if (world(entity) != nullptr && self->physics_ != nullptr) {
            self->physics_->setHorizontalVelocity(entity, *self->ecs_, glm::vec2(velocity.x, velocity.z));
            self->physics_->setVerticalVelocity(entity, *self->ecs_, velocity.y);
        }
        py_newnone(py_retval());
        return true;
    }

    static bool time(int argc, py_Ref argv) {
        PY_CHECK_ARGC(0);
        PythonScripting* self = host();
        py_newfloat(py_retval(), self ? self->clock_ : 0.0);
        return true;
    }

    static bool addHandler(int argc, py_Ref argv, const char* list) {
        PY_CHECK_ARGC(1);
        if (!py_callable(py_arg(0))) return py_exception(tp_TypeError, "expected a function");
        py_GlobalRef events = py_getmodule("events");
        py_ItemRef handlers = events ? py_getdict(events, py_name(list)) : nullptr;
        if (handlers == nullptr || !py_istype(handlers, tp_list)) {
            return py_exception(tp_RuntimeError, "the events module was changed by the script");
        }
        py_list_append(handlers, py_arg(0));
        py_assign(py_retval(), py_arg(0));
        return true;
    }
    static bool onUpdate(int argc, py_Ref argv) { return addHandler(argc, argv, "_update"); }
    static bool onCollision(int argc, py_Ref argv) { return addHandler(argc, argv, "_collision"); }
    static bool onUnload(int argc, py_Ref argv) { return addHandler(argc, argv, "_unload"); }

    static void install() {
        py_Callbacks* callbacks = py_callbacks();
        callbacks->importfile = denyImport;
        callbacks->lazyimport = nullptr;
        callbacks->print = onPrint;
        callbacks->flush = onFlush;
        callbacks->getchr = noInput;

        py_GlobalRef builtins = py_getmodule("builtins");
        py_bindfunc(builtins, "exit", exitBlocked);
        py_bindfunc(builtins, "input", inputBlocked);
        if (py_GlobalRef timeModule = py_getmodule("time")) py_bindfunc(timeModule, "sleep", sleepBlocked);

        py_GlobalRef worldModule = py_newmodule("world");
        py_bindfunc(worldModule, "find", find);
        py_bindfunc(worldModule, "name", name);
        py_bindfunc(worldModule, "exists", exists);
        py_bindfunc(worldModule, "get_position", getPosition);
        py_bindfunc(worldModule, "set_position", setPosition);
        py_bindfunc(worldModule, "apply_impulse", applyImpulse);
        py_bindfunc(worldModule, "set_velocity", setVelocity);
        py_bindfunc(worldModule, "time", time);

        py_GlobalRef eventsModule = py_newmodule("events");
        for (const char* list : {"_update", "_collision", "_unload"}) {
            py_newlist(py_r0());
            py_setdict(eventsModule, py_name(list), py_r0());
        }
        py_bindfunc(eventsModule, "on_update", onUpdate);
        py_bindfunc(eventsModule, "on_collision", onCollision);
        py_bindfunc(eventsModule, "on_unload", onUnload);

        py_setdict(builtins, py_name("world"), worldModule);
        py_setdict(builtins, py_name("events"), eventsModule);
    }
};

PythonScripting::~PythonScripting() { unloadAll(); }

std::string PythonScripting::capabilityReport() {
    PythonScripting python;
    std::string probeOutput;
    python.setOutputCallback([&](const std::string& line) { probeOutput += line + "\n"; });
    const char* probes[][2] = {
        {"import os", "import os"},
        {"open a file", "open('/etc/hostname')"},
        {"time.sleep", "import time\ntime.sleep(0)"},
        {"input", "input()"},
        {"exit", "exit()"},
    };
    std::string report = "Kronos Python scripting\n";
    report += std::string("  engine:      PocketPy ") + PK_VERSION + " (a small Python 3, not CPython)\n";
    report += "  scripts:     up to " + std::to_string(kMaxScripts) + ", one VM each\n";
    report += "  memory:      " + std::to_string(python.maxMemoryBytes_ / (1024 * 1024)) + " MB per script\n";
    report += "  time:        " + std::to_string(static_cast<int>(python.maxMillisPerTick_)) + " ms per tick, " +
              std::to_string(static_cast<int>(python.maxMillisForLoad_)) + " ms to load\n";
    report += "  builtins:    world, events\n  probes (what a script can do):\n";
    for (const auto& probe : probes) {
        const ScriptId id = python.load("probe", probe[1]);
        report += std::string("    ") + probe[0] + ": " + (id == kInvalidScript ? "blocked" : "ALLOWED") + "\n";
        python.unloadAll();
    }
    return report;
}

PythonScripting::Script* PythonScripting::find(ScriptId id) {
    for (auto& script : scripts_) {
        if (script.id == id) return &script;
    }
    return nullptr;
}

const PythonScripting::Script* PythonScripting::find(ScriptId id) const {
    for (const auto& script : scripts_) {
        if (script.id == id) return &script;
    }
    return nullptr;
}

void PythonScripting::emit(const std::string& line) {
    if (output_) {
        output_(line);
    } else {
        std::fprintf(stderr, "%s\n", line.c_str());
    }
}

void PythonScripting::report(Script& script, const std::string& message) {
    lastError_ = message;
    if (std::find(script.reported.begin(), script.reported.end(), message) != script.reported.end()) return;
    if (script.reported.size() >= kMaxReported) return;
    script.reported.push_back(message);
    emit("[python] " + script.name + ": " + message);
}

PythonScripting::CallResult PythonScripting::protectedCall(Script& script, bool (*fn)(void*), void* user, double millis) {
    kronos_py_use_slot(script.slot);
    kronos_py_result result = kronos_py_protected(fn, user, static_cast<int64_t>(std::ceil(millis)));
    kronos_py_stop reason = kronos_py_stop_reason(script.slot);
    if (result == KRONOS_PY_KILLED || reason != KRONOS_PY_STOP_NONE) {
        if (result == KRONOS_PY_ERROR) py_clearexc(nullptr);
        char limit[64];
        std::snprintf(limit, sizeof(limit), "%.0f ms", millis);
        stop(script, reason == KRONOS_PY_STOP_TIME ? "ran longer than its " + std::string(limit) + " limit"
                                                   : "used more than its " + megabytes(maxMemoryBytes_) + " memory limit");
        return CallResult::Stopped;
    }
    if (result == KRONOS_PY_OK) return CallResult::Ok;
    char* text = py_formatexc();
    std::string message = text != nullptr ? text : "unknown error";
    kronos_py_free(text);
    py_clearexc(nullptr);
    report(script, message);
    return CallResult::Error;
}

void PythonScripting::stop(Script& script, const std::string& why) {
    lastError_ = "stopped: " + why;
    emit("[python] " + script.name + " stopped: " + why);
    release(script);
}

void PythonScripting::release(Script& script) {
    if (script.slot <= 0) return;
    SlotOwner& owner = g_slots[script.slot];
    if (!owner.printLine.empty()) Bindings::flushPrint(owner);
    kronos_py_reset_slot(script.slot);
    owner = SlotOwner{};
    script.slot = 0;
}

PythonScripting::ScriptId PythonScripting::load(const std::string& chunkName, const std::string& source) {
    std::lock_guard<std::recursive_mutex> lock(pyMutex());
    int slot = 0;
    for (int i = 1; i < KRONOS_PY_SLOTS; ++i) {
        if (g_slots[i].host == nullptr) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        lastError_ = "too many Python scripts (the limit is " + std::to_string(kMaxScripts) + ")";
        emit("[python] " + chunkName + ": " + lastError_);
        return kInvalidScript;
    }

    Script script;
    script.id = nextId_++;
    script.slot = slot;
    script.name = chunkName;
    g_slots[slot].host = this;
    g_slots[slot].id = script.id;
    kronos_py_use_slot(slot);
    kronos_py_set_limit(slot, maxMemoryBytes_);
    Bindings::install();

    ExecArgs args{source.c_str(), chunkName.c_str()};
    CallResult result = protectedCall(script, runExec, &args, maxMillisForLoad_);
    if (result != CallResult::Ok) {
        release(script);
        return kInvalidScript;
    }
    scripts_.push_back(std::move(script));
    return scripts_.back().id;
}

void PythonScripting::fireHandlers(const char* list, int argc, const double* args, bool asInts, double millis) {
    std::lock_guard<std::recursive_mutex> lock(pyMutex());
    for (auto& script : scripts_) {
        for (int i = 0; script.slot != 0; ++i) {
            kronos_py_use_slot(script.slot);
            py_GlobalRef events = py_getmodule("events");
            py_ItemRef handlers = events ? py_getdict(events, py_name(list)) : nullptr;
            if (handlers == nullptr || !py_istype(handlers, tp_list) || i >= py_list_len(handlers)) break;
            CallArgs call{};
            call.fn = *py_list_getitem(handlers, i);
            call.argc = argc;
            for (int a = 0; a < argc; ++a) {
                if (asInts) {
                    py_newint(&call.argv[a], static_cast<py_i64>(args[a]));
                } else {
                    py_newfloat(&call.argv[a], args[a]);
                }
            }
            protectedCall(script, runCall, &call, millis);
        }
    }
    scripts_.erase(std::remove_if(scripts_.begin(), scripts_.end(), [](const Script& s) { return s.slot == 0; }),
                   scripts_.end());
}

void PythonScripting::tick(float dt) {
    clock_ += dt;
    double arg = dt;
    fireHandlers("_update", 1, &arg, false, maxMillisPerTick_);
}

void PythonScripting::fireCollision(uint32_t entityA, uint32_t entityB) {
    double args[2] = {static_cast<double>(entityA), static_cast<double>(entityB)};
    fireHandlers("_collision", 2, args, true, maxMillisPerTick_);
}

void PythonScripting::unload(ScriptId id) {
    std::lock_guard<std::recursive_mutex> lock(pyMutex());
    Script* script = find(id);
    if (script == nullptr) return;
    for (int i = 0; script->slot != 0; ++i) {
        kronos_py_use_slot(script->slot);
        py_GlobalRef events = py_getmodule("events");
        py_ItemRef handlers = events ? py_getdict(events, py_name("_unload")) : nullptr;
        if (handlers == nullptr || !py_istype(handlers, tp_list) || i >= py_list_len(handlers)) break;
        CallArgs call{};
        call.fn = *py_list_getitem(handlers, i);
        protectedCall(*script, runCall, &call, kUnloadMillis);
    }
    release(*script);
    scripts_.erase(std::remove_if(scripts_.begin(), scripts_.end(), [id](const Script& s) { return s.id == id; }),
                   scripts_.end());
}

void PythonScripting::unloadAll() {
    while (!scripts_.empty()) unload(scripts_.front().id);
}

bool PythonScripting::isRunning(ScriptId id) const {
    const Script* script = find(id);
    return script != nullptr && script->slot != 0;
}

size_t PythonScripting::memoryUsed(ScriptId id) const {
    std::lock_guard<std::recursive_mutex> lock(pyMutex());
    const Script* script = find(id);
    return script != nullptr && script->slot != 0 ? kronos_py_used(script->slot) : 0;
}

} // namespace engine::core
