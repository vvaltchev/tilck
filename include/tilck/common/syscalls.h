/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <sys/syscall.h>         // system header

#define TILCK_CMD_SYSCALL    499

enum tilck_cmd {

   TILCK_CMD_RUN_SELFTEST        = 0,
   TILCK_CMD_GCOV_GET_NUM_FILES  = 1,
   TILCK_CMD_GCOV_FILE_INFO      = 2,
   TILCK_CMD_GCOV_GET_FILE       = 3,
   TILCK_CMD_QEMU_POWEROFF       = 4,
   TILCK_CMD_SET_SAT_ENABLED     = 5,

   /*
    * Slots 6..8: previously the in-kernel debugpanel TUI entry points.
    * Now deprecated: the userspace `dp` tool implements the TUI using
    * the TILCK_CMD_DP_* sub-commands below. Slots remain so syscall
    * numbers don't shift; their handlers stay NULL → -EINVAL.
    */
   TILCK_CMD_DEBUG_PANEL         = 6,    /* deprecated */
   TILCK_CMD_TRACING_TOOL        = 7,    /* deprecated */
   TILCK_CMD_PS_TOOL             = 8,    /* deprecated */

   TILCK_CMD_DEBUGGER_TOOL       = 9,    /* panic-time mini debugger */
   TILCK_CMD_CALL_FUNC_0         = 10,
   TILCK_CMD_GET_VAR_LONG        = 11,
   TILCK_CMD_BUSY_WAIT           = 12,

   /*
    * Sub-commands used by the userspace `dp` tool. All registered at
    * runtime by the in-kernel `debugpanel` module via register_tilck_cmd().
    * If the module is not loaded, these slots are NULL and the syscall
    * returns -EINVAL.
    */
   TILCK_CMD_DP_GET_TASKS              = 13,
   TILCK_CMD_DP_GET_HEAPS              = 14,
   TILCK_CMD_DP_GET_KMALLOC_CHUNKS     = 15,
   TILCK_CMD_DP_GET_IRQ_STATS          = 16,
   TILCK_CMD_DP_GET_MEM_MAP            = 17,
   TILCK_CMD_DP_GET_MEM_GLOBAL_STATS   = 18,
   TILCK_CMD_DP_GET_MTRRS              = 19,
   TILCK_CMD_DP_TRACE_SET_FILTER       = 20,
   TILCK_CMD_DP_TRACE_GET_FILTER       = 21,
   TILCK_CMD_DP_TASK_SET_TRACED        = 22,
   TILCK_CMD_DP_TRACE_GET_STATS        = 23,
   TILCK_CMD_DP_TRACE_GET_SYS_NAME     = 24,
   TILCK_CMD_DP_TRACE_SET_ENABLED      = 25,
   TILCK_CMD_DP_TRACE_SET_FORCE_EXP_BLOCK = 26,
   TILCK_CMD_DP_TRACE_SET_DUMP_BIG_BUFS = 27,
   TILCK_CMD_DP_TRACE_SET_PRINTK_LVL   = 28,
   TILCK_CMD_DP_TRACE_GET_TRACED_BITMAP = 29,
   TILCK_CMD_DP_TRACE_GET_IN_BUF_COUNT = 30,
   TILCK_CMD_DP_TASK_GET_TRACED_TIDS_AND_CLEAR = 31,

   /*
    * Slot 32 was previously TILCK_CMD_DP_TRACE_RENDER_EVENT — a
    * kernel-side renderer used while we were still moving the
    * tracer TUI to userspace. Rendering now lives entirely in the
    * userspace `dp` tool; the kernel exposes the static metadata
    * via /syst/tracing/metadata instead. The slot remains so
    * subsequent ids don't shift; the handler stays NULL → -EINVAL.
    */
   TILCK_CMD_DP_TRACE_RENDER_EVENT     = 32,    /* deprecated */

   /*
    * Test-only sub-commands used by `tracer --test` (Tier 2 event
    * injection). TILCK_CMD_DP_TRACE_SET_TEST_MODE flips a kernel flag
    * that must be on before TILCK_CMD_DP_TRACE_INJECT_EVENT will
    * accept a user-supplied struct trace_event into the ring buffer.
    * Without the flag set, INJECT_EVENT returns -EPERM, so a release
    * build can't be flooded by an untrusted writer.
    */
   TILCK_CMD_DP_TRACE_SET_TEST_MODE    = 33,
   TILCK_CMD_DP_TRACE_INJECT_EVENT     = 34,

   /*
    * Runtime snapshot for the dp Options panel's Run-time section
    * (hypervisor flag, framebuffer info, tty count, clock-resync
    * stats). Fed by debugpanel/dp_data.c.
    */
   TILCK_CMD_DP_GET_RUNTIME_INFO       = 35,

   /* Number of elements in the enum */
   TILCK_CMD_COUNT               = 36,
};

#if defined(__x86_64__)

   #define STAT_SYSCALL_N      SYS_stat
   #define LSTAT_SYSCALL_N     SYS_lstat
   #define FSTAT_SYSCALL_N     SYS_fstat
   #define FCNTL_SYSCALL_N     SYS_fcntl
   #define MMAP_SYSCALL_N      SYS_mmap

#elif defined(__i386__)

   #define STAT_SYSCALL_N      SYS_stat64
   #define LSTAT_SYSCALL_N     SYS_lstat64
   #define FSTAT_SYSCALL_N     SYS_fstat64
   #define FCNTL_SYSCALL_N     SYS_fcntl64
   #define MMAP_SYSCALL_N      192

   #undef SYS_getuid
   #undef SYS_getgid
   #undef SYS_geteuid
   #undef SYS_getegid

   #define SYS_getuid            199
   #define SYS_getgid            200
   #define SYS_geteuid           201
   #define SYS_getegid           202

   #define SYS_getuid16           24
   #define SYS_getgid16           47
   #define SYS_geteuid16          49
   #define SYS_getegid16          50

   #undef SYS_lchown
   #undef SYS_fchown
   #undef SYS_chown

   #define SYS_lchown            198
   #define SYS_fchown            207
   #define SYS_chown             212

   #define SYS_lchown16           16
   #define SYS_fchown16           95
   #define SYS_chown16           182

   #define SYS_llseek            140

   #undef SYS_gettimeofday
   #define SYS_gettimeofday       78

#elif defined(__aarch64__) && (defined(KERNEL_TEST) || defined(TESTING))

   /* Allow this just for the unit tests */

#elif defined(__riscv)

   /* TODO */

#else

   #error Architecture not supported

#endif
