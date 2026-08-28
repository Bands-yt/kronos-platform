#pragma once

// DRAFT SCAFFOLDING -- not wired into the build, not implemented.
// See polyglot/README.md.
//
// Goal: one breakpoint/step session that can move from a Luau script
// frame into native C++ (or a future WASM/TS frame) and back, instead of
// each language runtime owning a completely separate debugger.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::polyglot {

enum class RuntimeKind : uint8_t {
    NativeCpp,
    Luau,
    TypeScript, // no host runtime exists yet -- see README
    WasmRustOrZig,
};

struct StackFrame {
    RuntimeKind runtime;
    std::string functionName;
    std::string sourceFile;
    uint32_t sourceLine;
    // Real cross-language jump point -- non-null only on the frame where
    // control genuinely crossed a language boundary (e.g. a Luau call
    // into a C++-registered binding), so a debugger UI can render a
    // real "step into native" affordance exactly where one exists,
    // not on every frame.
    std::optional<uint32_t> callerFrameIndex;
};

struct Breakpoint {
    RuntimeKind runtime;
    std::string sourceFile;
    uint32_t sourceLine;
    std::string condition; // empty = unconditional
};

// The real, unresolved design question: Luau already has its own real
// debug hooks (lua_Debug, breakpoint-capable today, see
// core::Scripting.hpp); a native C++ frame has no equivalent without a
// real DWARF/symbol-based unwinder. This interface assumes some future
// per-runtime adapter can produce a StackFrame list on demand -- what
// that adapter looks like for native C++ specifically is real,
// unsolved, non-trivial work, not sketched here.
class DebugSession {
public:
    void setBreakpoint(Breakpoint breakpoint);
    void clearBreakpoint(const std::string& sourceFile, uint32_t sourceLine);

    // TODO: register one adapter per RuntimeKind this session can
    // actually pause/inspect. A session with zero adapters registered is
    // a real, honest no-op -- never a crash.
    [[nodiscard]] std::vector<StackFrame> captureStack() const;

    void stepInto();
    void stepOver();
    void stepOut();
    void resume();

private:
    std::vector<Breakpoint> breakpoints_;
};

} // namespace engine::polyglot
