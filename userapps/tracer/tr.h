/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace tracer renderer — public API exposed by tr_meta.c,
 * tr_dump.c, and tr_render.c.
 *
 * The renderer reads kernel-emitted trace events from /syst/tracing/
 * events, looks up per-syscall metadata loaded once at startup from
 * /syst/tracing/metadata, and produces colored ANSI strings for the
 * dp tracer screen. The kernel-side renderer (modules/debugpanel/
 * dp_trace_render.c) is going away in a follow-up commit.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <tilck/common/tracing/wire.h>
#include <tilck/common/dp_abi.h>

/* ------------------------------- meta ------------------------------- */

/*
 * Open /syst/tracing/metadata, validate magic + version, build an
 * in-process by-sys_n index. Idempotent: the second call is a no-op.
 * Returns 0 on success, negative errno on failure (printed to stderr).
 *
 * Tolerated failure modes: the metadata file is missing (kernel built
 * with MOD_tracing=0; the tracer can't really run anyway), or wrong
 * magic/version. In both cases the renderer falls back to name-only
 * output for every syscall.
 */
int tr_meta_init(void);

/*
 * Lookup syscall metadata by sys_n. Returns NULL when the kernel has
 * no metadata for that syscall (typical on archs where
 * tracing_metadata.c hasn't been filled in yet) — callers must handle
 * that case and render a bare "name()".
 */
const struct tr_wire_syscall *tr_get_sys_info(unsigned sys_n);

/*
 * Lookup per-ptype info by stable type_id (enum tr_ptype_id). Returns
 * NULL for TR_PT_NONE or out-of-range ids.
 */
const struct tr_wire_ptype_info *tr_get_ptype_info(unsigned type_id);

/* --------------------------- runtime knobs --------------------------- */

/*
 * Mirror the kernel-side __force_exp_block / __tracing_dump_big_bufs
 * flags userspace-side. The dp tracer pushes them in after every
 * dp_cmd_get_stats() refresh so that the renderer's behavior matches
 * what the user sees in the banner.
 */
void tr_set_force_exp_block(bool v);
void tr_set_dump_big_bufs(bool v);

bool tr_get_force_exp_block(void);
bool tr_get_dump_big_bufs(void);

/* ----------------------------- dumping ------------------------------ */

/*
 * Dispatch helpers used by the renderer. Return false if the
 * destination buffer was too small for the formatted value (mirrors
 * the kernel ptype callback contract).
 */
bool tr_dump_from_val(unsigned type_id,
                      unsigned long val,
                      long helper,
                      char *dst, size_t dst_size);

bool tr_dump(unsigned type_id,
             unsigned long orig,
             char *data, long data_size,
             long helper,
             char *dst, size_t dst_size);

/* Signal / errno name tables — verbatim port of the kernel-side
 * lookups, used by the renderer for formatting events. */
const char *tr_get_errno_name(int err);
const char *tr_get_signal_name(int signum);

/* Layer 2 — context-dependent struct argp/arg dump callbacks
 * (defined in tr_dump_ioctl.c). The helper carries the cmd /
 * request value selected by the metadata's COMPLEX_PARAM. */
bool tr_dump_ioctl_argp(unsigned long orig,
                        char *data, long data_size,
                        long helper,
                        char *dst, size_t dst_size);

bool tr_dump_fcntl_arg(unsigned long orig,
                       char *data, long data_size,
                       long helper,
                       char *dst, size_t dst_size);

/* ----------------------------- render ------------------------------- */

/*
 * Render a single trace event into out[0..out_sz-1] as colored ANSI
 * text. Returns the number of bytes written (excluding any trailing
 * NUL the renderer adds). The dp_render_ctx is read-modify-write so
 * the trace_printk multi-line continuation logic survives across
 * calls within a single live-tracing session.
 */
int tr_render_event(const struct dp_trace_event *e,
                    char *out,
                    size_t out_sz,
                    struct dp_render_ctx *ctx);

/* ----------------------------- entry -------------------------------- */

/*
 * Standalone entry point for the tracer binary — implemented in
 * screen_tracing.c, called by main.c. Sets up the terminal, reads
 * trace events, renders them; returns when the user presses 'q'.
 */
int dp_run_tracer(void);
