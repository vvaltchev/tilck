/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/config_debug.h>
#include <tilck/kernel/fs/vfs_base.h>

#define PIPE_BUF_SIZE   4096

struct pipe;

struct pipe *create_pipe(void);
void destroy_pipe(struct pipe *p);
fs_handle pipe_create_read_handle(struct pipe *p);
fs_handle pipe_create_write_handle(struct pipe *p);

#if KRN_HANG_DETECTION
/*
 * Debug helpers used by the hang detector in kernel/debug.c. Both are
 * compiled in only when KRN_HANG_DETECTION is set; the dump path in the
 * detector calls them directly, no other caller.
 */

/*
 * If `h` is a pipe-handle, returns the underlying pipe and sets
 * *is_write_end to indicate which end. Returns NULL for non-pipe
 * handles. Discriminates by file_ops pointer comparison so it's safe
 * even if other kernel-fs handles share the same kobj layout.
 */
struct pipe *debug_get_pipe_for_handle(fs_handle h, bool *is_write_end);

/*
 * If `obj` matches one of the live pipes' conds (not_full / not_empty /
 * err) or its mutex, prints that pipe's read/write handle counts,
 * buffer fill, and the per-pipe ring of recent dup/close events.
 *
 * Caller MUST hold preemption disabled (the detector does this around
 * the whole task-state dump). No-op when `obj` doesn't match anything
 * — safe to call for every wait-object pointer in sight.
 */
void debug_dump_pipe_state_for_obj(void *obj);
#endif
