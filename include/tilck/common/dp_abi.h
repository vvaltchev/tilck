/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shared ABI between the in-kernel `debugpanel` module and the userspace
 * `dp` tool. All sub-commands listed below are dispatched through
 * sys_tilck_cmd() (syscall #499); see <tilck/common/syscalls.h> for the
 * TILCK_CMD_DP_* enum values.
 *
 * Both sides build from the same source tree, so there is no ABI
 * versioning. Fixed-size types are used throughout for clarity.
 */

#pragma once

#include <tilck/common/basic_defs.h>

#define DP_TASK_NAME_MAX     32
#define DP_TRACE_FILTER_MAX  256

/*
 * Userspace mirror of <tilck/mods/tracing.h>'s trace_event ABI. The
 * kernel emits these directly into /syst/tracing/events, so the
 * struct layouts here MUST stay byte-compatible with the kernel
 * versions. dp_data.c sanity-checks this with STATIC_ASSERTs.
 *
 * The userspace dp tool reads one struct dp_trace_event per read()
 * from /syst/tracing/events. DP_PRINTK_BUF_SIZE matches the kernel's
 * struct printk_event_data.buf size; DP_KERNEL_FMT0_SIZE matches the
 * kernel's anonymous fmt0 union (64 + 64 + 32 + 16) inside struct
 * syscall_event_data — that's where the kernel stashes IN-direction
 * parameter previews. We don't render parameters in this MVP and
 * just expose the storage as an opaque blob.
 */
#define DP_PRINTK_BUF_SIZE       192
#define DP_KERNEL_FMT0_SIZE      176

enum dp_trace_event_type {
   dp_te_invalid           = 0,
   dp_te_sys_enter         = 1,
   dp_te_sys_exit          = 2,
   dp_te_printk            = 3,
   dp_te_signal_delivered  = 4,
   dp_te_killed            = 5,
};

struct dp_syscall_event_data {

   u32   sys;
   long  retval;
   ulong args[6];

   char  saved_params[DP_KERNEL_FMT0_SIZE];
};

struct dp_printk_event_data {
   short level;
   u8    in_irq;
   u8    _pad;
   char  buf[DP_PRINTK_BUF_SIZE];
};

struct dp_signal_event_data {
   int signum;
};

struct dp_trace_event {

   int  type;          /* enum dp_trace_event_type */
   int  tid;
   u64  sys_time;

   union {
      struct dp_syscall_event_data sys_ev;
      struct dp_printk_event_data  p_ev;
      struct dp_signal_event_data  sig_ev;
   };
};

/*
 * Aggregate snapshot of the tracing module's runtime state — counts
 * + a couple of toggles + the printk-cutoff level. The userspace
 * tracer panel reads this once per refresh to populate the stats
 * banner.
 */
struct dp_trace_stats {

   u8   force_exp_block;     /* 0/1 — "always ENTER+EXIT" mode */
   u8   dump_big_bufs;       /* 0/1 — "Big bufs" toggle */
   u8   enabled;             /* 0/1 — global tracing on/off */
   u8   reserved;
   s32  printk_lvl;          /* trace_printk verbosity threshold */
   s32  sys_traced_count;    /* number of syscalls in the filter */
   s32  tasks_traced_count;  /* number of tasks with .traced=true */
};
#define DP_IRQ_VECTORS       256
#define DP_SYS_NAME_MAX      48

/*
 * One row in the Tasks panel.
 *
 * `name` carries either:
 *   - For user processes: the contents of process->debug_cmdline
 *     (truncated to DP_TASK_NAME_MAX-1 if needed).
 *   - For kthreads:       "<name>"  or  "<wth:name(prio)>"  for worker
 *                         threads.
 */
struct dp_task_info {

   s32  tid;
   s32  pid;          /* Tilck: pid == tgid */
   s32  pgid;
   s32  sid;
   s32  parent_pid;
   s32  tty;          /* tty number, or 0 for none/kthreads */
   u8   state;        /* enum task_state value */
   u8   stopped;      /* 0/1 */
   u8   traced;       /* 0/1 */
   u8   is_kthread;   /* 0/1 */
   u8   is_worker;    /* 0/1 (worker thread) */
   u8   reserved[3];

   char name[DP_TASK_NAME_MAX];
};

/*
 * One row in the Heaps panel. Mirrors `struct debug_kmalloc_heap_info`
 * but with fixed-size types.
 */
struct dp_heap_info {

   u64  vaddr;
   u64  size;
   u64  mem_allocated;
   u32  min_block_size;
   u32  alloc_block_size;
   s32  region;          /* -1 if not associated with a region */
   u32  reserved;
};

/*
 * Header populated alongside the heaps array (counts of "small heaps" the
 * kmalloc allocator carves out on demand). Mirrors
 * `struct kmalloc_small_heaps_stats`.
 */
struct dp_small_heaps_stats {

   s32  tot_count;
   s32  peak_count;
   s32  not_full_count;
   s32  peak_not_full_count;
};

/*
 * One row in the MemChunks panel (only when KRN_KMALLOC_HEAVY_STATS is
 * compiled in; otherwise the GET_KMALLOC_CHUNKS sub-command returns
 * -ENOTSUP).
 */
struct dp_kmalloc_chunk {

   u64  size;
   u64  count;
   u64  max_waste;
   u32  max_waste_p;     /* per-mille (0..1000) */
   u32  reserved;
};

/* IRQs panel snapshot. */
struct dp_irq_stats {

   u32  slow_timer_count;            /* slow_timer_irq_handler_count */
   u32  spur_irq_count;
   u64  ticks_at_snapshot;           /* get_ticks() at sample time */
   u32  unhandled_count[DP_IRQ_VECTORS];
   u32  unmasked_mask_lo16;          /* bitmask of legacy IRQ 0..15 */
};

/*
 * One row in the MemMap panel. Mirrors `struct mem_region` from
 * <tilck/kernel/system_mmap.h>.
 */
struct dp_mem_region {

   u64  addr;
   u64  len;
   u32  type;            /* multiboot_memory_map_t's type */
   u32  extra;           /* MEM_REG_EXTRA_* bitmask */
};

/* Aggregate counters shown above the MemMap. */
struct dp_mem_global_stats {

   u64  tot_usable;
   u64  kmalloc_used;
   u64  ramdisk_used;
   u64  kernel_used;
};

/* One row in the MTRRs panel (x86 only). */
struct dp_mtrr_entry {

   u64  base;
   u64  size_kb;         /* 0 if range spans multiple non-contiguous bits */
   u8   mem_type;        /* MEM_TYPE_* (UC=0, WC=1, WT=4, WP=5, WB=6) */
   u8   one_block;       /* 1 if size_kb is exact, 0 if multi-block */
   u8   reserved[6];
};

/*
 * Header for GET_MTRRS. `supported` is 0 on non-x86 hosts (the syscall
 * returns -ENOTSUP in that case) or when the CPU exposes no variable MTRRs.
 */
struct dp_mtrr_info {

   u8   supported;
   u8   default_type;    /* MEM_TYPE_* */
   u8   reserved[6];
};

/* ----------------- sub-command argument conventions -----------------
 *
 * sys_tilck_cmd(int cmd_n, ulong a1, ulong a2, ulong a3, ulong a4)
 *
 * GET_TASKS:
 *   a1 = struct dp_task_info __user *buf
 *   a2 = ulong max_count
 *   returns: actual count written, or -errno
 *
 * GET_HEAPS:
 *   a1 = struct dp_heap_info __user *buf
 *   a2 = ulong max_count
 *   a3 = struct dp_small_heaps_stats __user *stats  (NULL allowed)
 *   returns: heap count written, or -errno
 *
 * GET_KMALLOC_CHUNKS:
 *   a1 = struct dp_kmalloc_chunk __user *buf
 *   a2 = ulong max_count
 *   returns: count, or -ENOTSUP if KRN_KMALLOC_HEAVY_STATS is off
 *
 * GET_IRQ_STATS:
 *   a1 = struct dp_irq_stats __user *out
 *   returns: 0, or -errno
 *
 * GET_MEM_MAP:
 *   a1 = struct dp_mem_region __user *buf
 *   a2 = ulong max_count
 *   returns: count, or -errno
 *
 * GET_MEM_GLOBAL_STATS:
 *   a1 = struct dp_mem_global_stats __user *out
 *   returns: 0, or -errno
 *
 * GET_MTRRS:
 *   a1 = struct dp_mtrr_entry __user *buf
 *   a2 = ulong max_count
 *   a3 = struct dp_mtrr_info __user *info  (NULL allowed)
 *   returns: count, or -ENOTSUP on non-x86
 *
 * TRACE_SET_FILTER:
 *   a1 = const char __user *expr   (NUL-terminated, ≤ DP_TRACE_FILTER_MAX)
 *   returns: 0, or -errno
 *
 * TRACE_GET_FILTER:
 *   a1 = char __user *buf
 *   a2 = ulong buf_size
 *   returns: number of bytes (excluding NUL), or -errno
 *
 * TASK_SET_TRACED:
 *   a1 = ulong tid
 *   a2 = ulong enabled (0 or 1)
 *   returns: 0, or -ESRCH if no such task, -EPERM for kthreads/self
 */
