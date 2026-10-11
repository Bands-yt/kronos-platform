// Prints where engine_tests crashed on Windows (module, function and offset per frame).
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>

namespace {

void printFrame(HANDLE process, DWORD64 address) {
    char moduleName[MAX_PATH] = "?";
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module)) {
        GetModuleFileNameA(module, moduleName, MAX_PATH);
    }
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    const bool named = SymFromAddr(process, address, &displacement, symbol) != FALSE;
    std::fprintf(stderr, "  %s+0x%llx  %s+0x%llx\n", moduleName,
                 static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)),
                 named ? symbol->Name : "?", static_cast<unsigned long long>(displacement));
}

LONG WINAPI reportCrash(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    std::fprintf(stderr, "\nengine_tests crashed: code 0x%08lX", record->ExceptionCode);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        std::fprintf(stderr, " (%s 0x%llx)", record->ExceptionInformation[0] == 1 ? "write to" : "read of",
                     static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    std::fprintf(stderr, "\n");
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    printFrame(process, reinterpret_cast<DWORD64>(record->ExceptionAddress));
    void* frames[32];
    const USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) printFrame(process, reinterpret_cast<DWORD64>(frames[i]));
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void installWindowsCrashInfo() { SetUnhandledExceptionFilter(reportCrash); }
#endif
