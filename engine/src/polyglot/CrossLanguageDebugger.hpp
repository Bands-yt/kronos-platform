#pragma once

// DRAFT SCAFFOLDING -- not wired into the build, not implemented.
// See polyglot/README.md.
//
// Goal: one breakpoint/step session that can move from a Luau script
// frame into native C++ and back, instead of each language runtime
// owning a completely separate debugger. Scoped to exactly these two
// runtimes -- see polyglot/README.md's "Why these two stay stubs" for
// why: this engine's actual multi-language surface is Luau only
// (core/Scripting.hpp, core/Script*Api.hpp); there is no TypeScript or
// WASM/Rust/Zig host runtime in this codebase to debug, so no
// RuntimeKind is scaffolded for one. Add one back only once a real
// second scripting host actually exists to design the adapter against.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::polyglot {

enum class RuntimeKind : uint8_t {
    NativeCpp,
    Luau,
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
