#include "kronos_pocketpy.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "pocketpy/interpreter/vm.h"
#include "pocketpy/pocketpy.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef _MSC_VER
#include <intrin.h>
#endif

// Every block carries a header so a VM's memory can be counted and freed in one go.
typedef struct Block {
    struct Block* prev;
    struct Block* next;
    size_t size;
    int64_t owner;
} Block;
_Static_assert(sizeof(Block) == 32, "Block header must keep 16-byte alignment");

#define GLOBAL_OWNER KRONOS_PY_SLOTS
static const size_t kGlobalLimit = 128u << 20;

static Block g_heads[KRONOS_PY_SLOTS + 1];
static size_t g_used[KRONOS_PY_SLOTS + 1];
static size_t g_limit[KRONOS_PY_SLOTS + 1];
static kronos_py_stop g_stop[KRONOS_PY_SLOTS + 1];
static int g_owner = GLOBAL_OWNER;
static bool g_ready;
static jmp_buf* g_jump;
static uintptr_t g_stackBase;
static size_t g_stackLimit = 384 * 1024;
static bool g_raising;
static int g_raisesAfterStop;

static void ensureLists(void) {
    if (g_ready) return;
    for (int i = 0; i <= KRONOS_PY_SLOTS; ++i) g_heads[i].prev = g_heads[i].next = &g_heads[i];
    g_ready = true;
}

static void blockLink(Block* b) {
    Block* head = &g_heads[b->owner];
    b->prev = head;
    b->next = head->next;
    head->next->prev = b;
    head->next = b;
    g_used[b->owner] += b->size;
}

static void blockUnlink(Block* b) {
    b->prev->next = b->next;
    b->next->prev = b->prev;
    g_used[b->owner] -= b->size;
}

static void armStop(int owner, kronos_py_stop reason) {
    if (g_stop[owner] == KRONOS_PY_STOP_NONE) g_stop[owner] = reason;
    if (pk_current_vm) pk_current_vm->watchdog_info.max_reset_time = 1;
}

// Growth past the limit stops the script at its next instruction; a single huge request jumps out now.
static void chargeOrKill(int owner, size_t grow) {
    if (owner == GLOBAL_OWNER || g_limit[owner] == 0) return;
    size_t limit = g_limit[owner];
    size_t hard = limit + limit / 4 + (4u << 20);
    if (g_used[owner] + grow > hard && g_jump) {
        if (g_stop[owner] == KRONOS_PY_STOP_NONE) g_stop[owner] = KRONOS_PY_STOP_MEMORY;
        longjmp(*g_jump, 1);
    }
    if (g_used[owner] + grow > limit) armStop(owner, KRONOS_PY_STOP_MEMORY);
}

static void* allocFor(int owner, size_t size) {
    ensureLists();
    chargeOrKill(owner, size);
    if (owner == GLOBAL_OWNER && g_owner != GLOBAL_OWNER) {
        // Interned names are shared and never freed, so they count against the script that made them.
        if (g_jump && g_used[GLOBAL_OWNER] + size > kGlobalLimit) {
            if (g_stop[g_owner] == KRONOS_PY_STOP_NONE) g_stop[g_owner] = KRONOS_PY_STOP_MEMORY;
            longjmp(*g_jump, 1);
        }
        chargeOrKill(g_owner, size);
        g_used[g_owner] += size;
    }
    Block* b = (Block*)malloc(sizeof(Block) + size);
    if (!b) {
        if (g_jump) {
            if (g_stop[owner] == KRONOS_PY_STOP_NONE) g_stop[owner] = KRONOS_PY_STOP_MEMORY;
            longjmp(*g_jump, 1);
        }
        return NULL;
    }
    b->size = size;
    b->owner = owner;
    blockLink(b);
    return b + 1;
}

void* kronos_py_malloc(size_t size) { return allocFor(g_owner, size); }
void* kronos_py_global_malloc(size_t size) { return allocFor(GLOBAL_OWNER, size); }

void* kronos_py_realloc(void* ptr, size_t size) {
    if (!ptr) return kronos_py_malloc(size);
    Block* b = (Block*)ptr - 1;
    if (size > b->size) chargeOrKill((int)b->owner, size - b->size);
    blockUnlink(b);
    Block* grown = (Block*)realloc(b, sizeof(Block) + size);
    if (!grown) {
        blockLink(b);
        if (g_jump) {
            if (g_stop[b->owner] == KRONOS_PY_STOP_NONE) g_stop[b->owner] = KRONOS_PY_STOP_MEMORY;
            longjmp(*g_jump, 1);
        }
        return NULL;
    }
    grown->size = size;
    blockLink(grown);
    return grown + 1;
}

void kronos_py_free(void* ptr) {
    if (!ptr) return;
    Block* b = (Block*)ptr - 1;
    blockUnlink(b);
    free(b);
}

clock_t kronos_py_clock(void) {
    // Wall time, not process CPU time: other engine threads must not use up a script's budget.
#ifdef _WIN32
    static LARGE_INTEGER freq, start;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    QueryPerformanceCounter(&now);
    double seconds = (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
#else
    static struct timespec start;
    struct timespec now;
    if (start.tv_sec == 0 && start.tv_nsec == 0) clock_gettime(CLOCK_MONOTONIC, &start);
    clock_gettime(CLOCK_MONOTONIC, &now);
    double seconds = (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_nsec - start.tv_nsec) * 1e-9;
#endif
    return (clock_t)((seconds + 1.0) * CLOCKS_PER_SEC);
}

static uintptr_t stackHere(void) {
#ifdef _MSC_VER
    return (uintptr_t)_AddressOfReturnAddress();
#else
    return (uintptr_t)__builtin_frame_address(0);
#endif
}

bool kronos_py_stack_low(void) {
    if (!g_stackBase) return false;
    uintptr_t at = stackHere();
    size_t depth = at < g_stackBase ? g_stackBase - at : at - g_stackBase;
    return depth > g_stackLimit;
}

bool kronos_py_check(void) {
    // Building the exception calls back into py_call, which must not raise again; nor may a pending one be replaced.
    if (!pk_current_vm || g_raising || py_checkexc()) return false;
    WatchdogInfo* info = &pk_current_vm->watchdog_info;
    bool late = info->max_reset_time > 0 && kronos_py_clock() > info->max_reset_time;
    if (!late && !kronos_py_stack_low()) return false;
    // A script that keeps catching the stop is cut off for good.
    if (g_stop[g_owner] != KRONOS_PY_STOP_NONE && ++g_raisesAfterStop > 1000 && g_jump) longjmp(*g_jump, 1);
    g_raising = true;
    if (late) {
        if (g_stop[g_owner] == KRONOS_PY_STOP_NONE) g_stop[g_owner] = KRONOS_PY_STOP_TIME;
        TimeoutError("watchdog timeout");
    } else {
        py_exception(tp_RecursionError, "maximum recursion depth exceeded");
    }
    g_raising = false;
    return true;
}

void py_watchdog_begin(py_i64 timeout) {
    pk_current_vm->watchdog_info.max_reset_time = kronos_py_clock() + (clock_t)(timeout * (CLOCKS_PER_SEC / 1000));
}

void py_watchdog_end(void) {
    if (g_stop[g_owner] == KRONOS_PY_STOP_NONE) pk_current_vm->watchdog_info.max_reset_time = 0;
}

void kronos_py_use_slot(int slot) {
    ensureLists();
    g_owner = GLOBAL_OWNER;
    py_initialize();
    g_owner = slot;
    py_switchvm(slot);
}

void kronos_py_reset_slot(int slot) {
    ensureLists();
    kronos_py_use_slot(slot);
    Block* head = &g_heads[slot];
    while (head->next != head) {
        Block* b = head->next;
        blockUnlink(b);
        free(b);
    }
    g_used[slot] = 0;
    g_stop[slot] = KRONOS_PY_STOP_NONE;
    memset(pk_current_vm, 0, sizeof(VM));
    VM__ctor(pk_current_vm);
}

void kronos_py_set_limit(int slot, size_t bytes) { g_limit[slot] = bytes; }
size_t kronos_py_used(int slot) { return g_used[slot]; }
size_t kronos_py_global_used(void) { return g_used[GLOBAL_OWNER]; }
kronos_py_stop kronos_py_stop_reason(int slot) { return g_stop[slot]; }
void kronos_py_set_stack_limit(size_t bytes) { g_stackLimit = bytes; }

kronos_py_result kronos_py_protected(bool (*fn)(void* user), void* user, int64_t timeoutMillis) {
    jmp_buf jump;
    jmp_buf* outerJump = g_jump;
    g_raisesAfterStop = 0;
    uintptr_t outerBase = g_stackBase;
    if (!outerBase) g_stackBase = stackHere();
    if (timeoutMillis > 0 && g_stop[g_owner] == KRONOS_PY_STOP_NONE) py_watchdog_begin(timeoutMillis);
    kronos_py_result result;
    g_jump = &jump;
    if (setjmp(jump) == 0) {
        result = fn(user) ? KRONOS_PY_OK : KRONOS_PY_ERROR;
    } else {
        result = KRONOS_PY_KILLED;
        g_raising = false;
    }
    g_jump = outerJump;
    g_stackBase = outerBase;
    if (timeoutMillis > 0 && result != KRONOS_PY_KILLED) py_watchdog_end();
    return result;
}

// Modules Kronos leaves out: raw memory (stdc), watchdog control (pkpy), the terminal, bytecode and pickle.
// stdc's type ids are fixed in PocketPy's enum, so the slots are filled with empty types no script can reach.
void pk__add_module_stdc(void) {
    for (py_Type t = tp_stdc_Memory; t <= tp_stdc_Bool; ++t) {
        py_Type made = py_newtype("_unused", tp_object, NULL, NULL);
        if (made != t) abort();
    }
}
void pk__add_module_pkpy(void) {}
void pk__add_module_conio(void) {}
void pk__add_module_picoterm(void) {}
void pk__add_module_dis(void) {}
void pk__add_module_pickle(void) {}
