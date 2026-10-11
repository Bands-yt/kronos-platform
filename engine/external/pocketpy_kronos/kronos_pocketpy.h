// Kronos glue for PocketPy: force-included into every PocketPy source, and used by core/PythonScripting.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#if defined(KRONOS_PY_INTERNAL) && defined(_MSC_VER) && !defined(__clang__)
// MSVC's __forceinline leaves no linkable copy of functions other files call (PyObject__dict, ...).
#include "pocketpy/export.h"
#undef PK_INLINE
#define PK_INLINE
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define KRONOS_PY_SLOTS 16

void* kronos_py_malloc(size_t size);
void* kronos_py_global_malloc(size_t size);
void* kronos_py_realloc(void* ptr, size_t size);
void kronos_py_free(void* ptr);
clock_t kronos_py_clock(void);

// Raises RecursionError/TimeoutError in the current VM and returns true when the script must stop.
bool kronos_py_check(void);
bool kronos_py_stack_low(void);

typedef enum {
    KRONOS_PY_OK = 0,
    KRONOS_PY_ERROR = 1,     // a Python exception is set
    KRONOS_PY_KILLED = 2,    // a huge allocation was refused; the VM must be reset
} kronos_py_result;

typedef enum {
    KRONOS_PY_STOP_NONE = 0,
    KRONOS_PY_STOP_TIME = 1,
    KRONOS_PY_STOP_MEMORY = 2,
} kronos_py_stop;

// Switches to `slot` (1..15), creating its VM on first use. Allocations are charged to the slot.
void kronos_py_use_slot(int slot);
// Frees everything the slot's VM allocated and builds a fresh VM in its place.
void kronos_py_reset_slot(int slot);
void kronos_py_set_limit(int slot, size_t bytes);
size_t kronos_py_used(int slot);
size_t kronos_py_global_used(void);

// Runs fn(user) with a stack guard, the time limit and the memory kill switch.
kronos_py_result kronos_py_protected(bool (*fn)(void* user), void* user, int64_t timeoutMillis);
kronos_py_stop kronos_py_stop_reason(int slot);
void kronos_py_set_stack_limit(size_t bytes);

#ifdef __cplusplus
}
#endif

#ifdef KRONOS_PY_INTERNAL
#ifdef KRONOS_PY_GLOBAL_ALLOC
#define PK_MALLOC(size) kronos_py_global_malloc(size)
#else
#define PK_MALLOC(size) kronos_py_malloc(size)
#endif
#define PK_REALLOC(ptr, size) kronos_py_realloc(ptr, size)
#define PK_FREE(ptr) kronos_py_free(ptr)
#define clock() kronos_py_clock()
#endif
