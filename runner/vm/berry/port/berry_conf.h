/* ChargeXcel's Berry configuration. Replaces upstream default/berry_conf.h.
 *
 * Berry is the on-device DLM script runtime. Every choice here is a memory
 * or isolation decision, and the firmware build checks the ones that matter:
 *
 *   - Memory comes from a fixed arena, never the system heap
 *     (BE_EXPLICIT_MALLOC/FREE/REALLOC -> dlm_arena_*, implemented by the
 *     host: a fixed arena on the unit, a capped counter on a desktop).
 *   - The VM heartbeat is what bounds a runaway script
 *     (BE_USE_PERF_COUNTERS + BE_VM_OBSERVABILITY_SAMPLING; see
 *     DlmScriptVm.cpp).
 *   - A script sees dlm, json, string and math, and nothing else: no os,
 *     sys, debug, introspect, file system or shared libraries.
 *
 * Regenerate the generate/ tables after changing any BE_USE_* value -- the
 * constant-object tables are built from this file.
 */
#ifndef BERRY_CONF_H
#define BERRY_CONF_H

#include <assert.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
void* dlm_arena_malloc(size_t size);
void dlm_arena_free(void* ptr);
void* dlm_arena_realloc(void* ptr, size_t size);
void dlm_arena_abort(void);
#ifdef __cplusplus
}
#endif

#ifndef BE_DEBUG
#define BE_DEBUG                        0
#endif

/* 32-bit int: RV32, and 2038 is long after this hardware. */
#define BE_INTGER_TYPE                  0
/* No FPU on the C3; halves every real cell. */
#define BE_USE_SINGLE_FLOAT             1
/* Matches dlm.http()'s 4 KB response cap -- nothing bigger reaches a script. */
#define BE_BYTES_MAX_SIZE               4096
/* Builtin classes and strings live in flash, not the arena. */
#define BE_USE_PRECOMPILED_OBJECT       1
#define BE_DEBUG_SOURCE_FILE            0
/* Line numbers in the error shown on /dlm; 2 = uint16 per
 * instruction, half the arena cost of 1. */
#define BE_DEBUG_RUNTIME_INFO           2
#define BE_DEBUG_VAR_INFO               0

/* REQUIRED: the VM heartbeat (VM_HEARTBEAT in be_vm.c) only compiles under
 * this. Sampling 14 = a hook call every 2^13 = 8,192 instructions, so a
 * budget check lands every few milliseconds rather than upstream's ~1 M. */
#define BE_USE_PERF_COUNTERS            1
#define BE_VM_OBSERVABILITY_SAMPLING    14

/* Stack slots, not bytes: caps VM stack growth well inside the arena. */
#define BE_STACK_TOTAL_MAX              1024
#define BE_STACK_FREE_MIN               10
#define BE_STACK_START                  50
#define BE_CONST_SEARCH_SIZE            50
#define BE_USE_STR_HASH_CACHE           0

/* No files: be_port.c stubs the whole be_sys.h API to fail. */
#define BE_USE_FILE_SYSTEM              0
/* Scripts are pasted as source into the web page and compiled on the unit,
 * so the compiler stays in. */
#define BE_USE_SCRIPT_COMPILER          1
#define BE_USE_BYTECODE_SAVER           0
#define BE_USE_BYTECODE_LOADER          0
#define BE_USE_SHARED_LIB               0
#define BE_USE_OVERLOAD_HASH            0
/* C-stack recursion bound for the compiler (upstream's ESP32 figure). */
#define BE_MAX_PARSER_DEPTH             25
#define BE_USE_DEBUG_HOOK               0
#define BE_USE_DEBUG_GC                 0
#define BE_USE_DEBUG_STACK              0
#define BE_USE_MEM_ALIGNED              0

/* Modules a script may import. Everything not listed as 1 does not exist. */
#define BE_USE_STRING_MODULE            1
#define BE_USE_JSON_MODULE              1
#define BE_USE_MATH_MODULE              1
#define BE_USE_TIME_MODULE              0
#define BE_USE_OS_MODULE                0
#define BE_USE_GLOBAL_MODULE            0
#define BE_USE_SYS_MODULE               0
#define BE_USE_DEBUG_MODULE             0
#define BE_USE_GC_MODULE                0
#define BE_USE_SOLIDIFY_MODULE          0
#define BE_USE_INTROSPECT_MODULE        0
#define BE_USE_STRICT_MODULE            0

/* Every allocation the VM makes goes to the arena. */
#define BE_EXPLICIT_ABORT               dlm_arena_abort
#define BE_EXPLICIT_EXIT(code)          dlm_arena_abort()
#define BE_EXPLICIT_MALLOC              dlm_arena_malloc
#define BE_EXPLICIT_FREE                dlm_arena_free
#define BE_EXPLICIT_REALLOC             dlm_arena_realloc

#define be_assert(expr)                 assert(expr)

#endif
