/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Wire format for /syst/tracing/metadata.
 *
 * The kernel's tracing module pre-builds this blob at module-init time
 * from its in-memory metadata + slot allocation tables and exposes it
 * as a single immutable read-only file. The userspace tracer (dp -t /
 * tracer) reads the blob once on startup and uses it to render syscall
 * events with parameter names + types + colors.
 *
 * Designed so the kernel never has to expose function pointers or
 * kernel-internal types: the data fields of struct syscall_info /
 * struct sys_param_info / struct sys_param_type collapse here into
 * fixed-width integers + a fixed-length string for the parameter
 * name. Function-pointer fields don't appear at all (the rendering is
 * userspace only).
 *
 * Both kernel and userspace include this header. No #ifdefs.
 */

#pragma once

#include <tilck/common/basic_defs.h>

#define TR_WIRE_MAGIC      0x54524d44u   /* 'T' 'R' 'M' 'D' (LE on disk) */
#define TR_WIRE_VERSION    2u
#define TR_MAX_PARAMS      6
#define TR_PNAME_MAX       16

/*
 * Stable ABI: append only, never reorder or remove. The kernel maps
 * its `&ptype_*` pointers to these enum values at build-blob time;
 * userspace dispatches its dump callbacks by the same value.
 *
 * If a future kernel adds a 17th ptype, bump TR_WIRE_VERSION and add
 * the new value at the end. Userspace can either accept (renders the
 * unknown ptype generically) or refuse to load.
 */
enum tr_ptype_id {
   TR_PT_INT             = 0,
   TR_PT_VOIDP           = 1,
   TR_PT_OCT             = 2,
   TR_PT_ERRNO_OR_VAL    = 3,
   TR_PT_ERRNO_OR_PTR    = 4,
   TR_PT_OPEN_FLAGS      = 5,
   TR_PT_DOFF64          = 6,
   TR_PT_WHENCE          = 7,
   TR_PT_INT32_PAIR      = 8,
   TR_PT_U64_PTR         = 9,
   TR_PT_SIGNUM          = 10,
   TR_PT_BUFFER          = 11,
   TR_PT_BIG_BUF         = 12,
   TR_PT_PATH            = 13,
   TR_PT_IOV_IN          = 14,
   TR_PT_IOV_OUT         = 15,

   /* Layer 1 — symbolic register-value ptypes (added in v2 wire). */
   TR_PT_MMAP_PROT        = 16,  /* PROT_READ|PROT_WRITE|...    */
   TR_PT_MMAP_FLAGS       = 17,  /* MAP_PRIVATE|MAP_ANONYMOUS|... */
   TR_PT_WAIT_OPTIONS     = 18,  /* WNOHANG|WUNTRACED|...       */
   TR_PT_ACCESS_MODE      = 19,  /* R_OK|W_OK|X_OK|F_OK         */
   TR_PT_IOCTL_CMD        = 20,  /* TCGETS / TIOCGWINSZ / ...   */
   TR_PT_FCNTL_CMD        = 21,  /* F_DUPFD / F_GETFL / ...     */
   TR_PT_SIGPROCMASK_HOW  = 22,  /* SIG_BLOCK / SIG_UNBLOCK / SIG_SETMASK */
   TR_PT_PRCTL_OPTION     = 23,  /* PR_SET_NAME / ...           */
   TR_PT_CLONE_FLAGS      = 24,  /* CLONE_VM|CLONE_FS|...       */
   TR_PT_MOUNT_FLAGS      = 25,  /* MS_RDONLY|MS_NOSUID|...     */
   TR_PT_MADVISE_ADVICE   = 26,  /* MADV_NORMAL / MADV_DONTNEED / ... */

   TR_PT_COUNT,                  /* not a real id; sentinel for the table */
   TR_PT_NONE            = 0xff,
};

/* Mirrors enum sys_param_kind in <tilck/mods/tracing.h>. */
enum tr_param_kind {
   TR_KIND_IN     = 0,
   TR_KIND_OUT    = 1,
   TR_KIND_IN_OUT = 2,
};

/* Mirrors enum sys_param_ui_type in <tilck/mods/tracing.h>. */
enum tr_ui_type {
   TR_UI_OTHER    = 0,
   TR_UI_INTEGER  = 1,
   TR_UI_STRING   = 2,
};

/* Per-parameter record. */
struct tr_wire_param {

   u8   type_id;         /* enum tr_ptype_id */
   u8   kind;            /* enum tr_param_kind */
   u8   invisible;       /* 0/1; renderer skips invisible params */
   u8   real_sz_in_ret;  /* 0/1; e.g. read() bytes-actually-read in retval */
   s8   helper_idx;      /* index into params[] for helper param, -1 if none */
   u8   reserved[3];
   char name[TR_PNAME_MAX]; /* NUL-terminated, truncated if longer */
};

/* Per-syscall record. */
struct tr_wire_syscall {

   u32  sys_n;
   u8   n_params;
   u8   exp_block;       /* 0/1; matches struct syscall_info.exp_block */
   u8   ret_type_id;     /* enum tr_ptype_id */
   u8   fmt;             /* 0 or 1 — picks slot offset / size table */
   s8   slots[TR_MAX_PARAMS]; /* slot index per param, -1 if unused */
   u8   reserved[2];
   struct tr_wire_param params[TR_MAX_PARAMS];
};

/* Per-ptype info: lets the userspace dispatcher know which ui_type
 * each ptype reports (for color decisions) and the slot size (used
 * mostly for sanity-checking; the kernel allocator already decided
 * which slot to use for each param of each syscall, and the result
 * is in tr_wire_syscall.slots[]). */
struct tr_wire_ptype_info {

   u8   ui_type;         /* enum tr_ui_type */
   u8   slot_size;       /* 0 if no save data (dump_from_val ptype) */
   u8   reserved[2];
   char name[12];
};

/* Header for the /syst/tracing/metadata file. The blob layout is:
 *   tr_wire_header
 *   tr_wire_ptype_info[ptype_count]
 *   tr_wire_syscall[syscall_count]
 */
struct tr_wire_header {

   u32  magic;           /* TR_WIRE_MAGIC */
   u16  version;         /* TR_WIRE_VERSION */
   u16  ptype_count;
   u32  syscall_count;
   u32  reserved;
};

/* Sanity: catch accidental size changes at build time. */
STATIC_ASSERT(sizeof(struct tr_wire_param)      == 24);
STATIC_ASSERT(sizeof(struct tr_wire_syscall)    == 16 + 6*24);
STATIC_ASSERT(sizeof(struct tr_wire_ptype_info) == 16);
STATIC_ASSERT(sizeof(struct tr_wire_header)     == 16);

/*
 * Slot offsets + sizes within the saved-params area of a syscall
 * trace event. Byte-counted (so arch-agnostic) and chosen to match
 * the fmt0 / fmt1 anonymous unions in struct syscall_event_data.
 *
 *   fmt0: 4 slots {64, 64, 32, 16} bytes wide, packed in that order.
 *   fmt1: 3 slots {128, 32, 16} bytes wide, packed in that order.
 *
 * `static const` so each translation unit gets its own copy in
 * .rodata; trivial size, no link-time concerns.
 */
static const u16 tr_fmt_offsets[2][4] = {
   { 0,  64, 128, 160 },
   { 0, 128, 160,   0 },
};
static const u16 tr_fmt_sizes[2][4] = {
   { 64, 64, 32, 16 },
   { 128, 32, 16,  0 },
};
