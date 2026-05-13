/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * `tracer --test` — Tier 2 synthetic-event injection tests.
 *
 * Tier 1 (test_live.c) exercises the trace pipeline end-to-end by
 * issuing real syscalls in a forked child. That covers the kernel
 * save callbacks + the ring + the userspace renderer, but you have
 * to round-trip through the actual syscall to hit a code path. Tier
 * 2 closes that gap by building events directly in userspace and
 * pushing them into the ring via TILCK_CMD_DP_TRACE_INJECT_EVENT.
 * The injection path skips per-task .traced, the syscall filter,
 * and the save callbacks — so the caller controls every byte of the
 * event, including the saved_params slot bytes.
 *
 * The injection cmd is gated by TILCK_CMD_DP_TRACE_SET_TEST_MODE: a
 * kernel flag (off by default) must be flipped on before any inject
 * call will succeed. Production builds therefore can't be flooded
 * by an unprivileged process; this driver toggles the flag on at
 * the top of tr_run_tier2_tests and clears it on exit.
 *
 * Coverage:
 *   - One test per enum trace_event_type
 *     (sys_enter, sys_exit, printk, signal_delivered, killed)
 *   - One test per symbolic register-value ptype family
 *     (signum, open_flags, mmap prot/flags, whence, sigprocmask_how)
 *   - One test per saved-bytes path (ptype_path string in slot 0,
 *     ptype_wstatus int in fmt0 slot 0, ioctl_argp / fcntl_arg
 *     context-dispatched cases)
 *   - Edge cases: "<fault>" marker, unknown sys_n fallback
 *
 * Wire-sanity prelude: read /syst/tracing/metadata, validate magic +
 * version. tr_meta_init does the validation already; we just check
 * its return code.
 *
 * i386-only: many tests reference SYS_open / SYS_mmap2 / SYS_fcntl64
 * / SYS__llseek — i386-specific syscall numbers that don't exist in
 * x86_64 / riscv64 musl. To keep the tracer binary building on those
 * archs, the whole file is wrapped in `#ifdef __i386__`; on other
 * arches tr_run_tier2_tests / tr_run_stress_test are stubs. Tier 2
 * coverage on x86_64 / riscv64 is a future refactor.
 */

#ifdef __i386__

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "tr.h"

#define EVENTS_PATH       "/syst/tracing/events"
#define RENDER_BUF_SZ     1024
#define STRESS_NEVENTS    10000

/* Pretend tid used by injected events. Distinct from any real task
 * so the renderer's by-tid scratch state stays uncontaminated when
 * the same tracer process also runs Tier 1. */
#define INJ_TID           90001
#define INJ_TID2          90002

/* ----------------------- TILCK_CMD wrappers -------------------------- */

static long
cmd_set_test_mode(int on)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_TEST_MODE,
                  (long)on, 0L, 0L, 0L);
}

static long
cmd_inject_event(const struct dp_trace_event *ev)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_INJECT_EVENT,
                  (long)ev, 0L, 0L, 0L);
}

/* ------------------------------ harness ------------------------------ */

static int events_fd = -1;
static int n_passed;
static int n_failed;

/*
 * Drain anything in the ring before each test so a previous test's
 * leftover events can't be mistaken for the current test's. The
 * inject path emits exactly one event per call, so each test reads
 * back exactly one event.
 */
static void
drain_ring(void)
{
   struct dp_trace_event ev;

   while (read(events_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) { }
}

/*
 * Build a blank event with the given type and a synthetic tid. The
 * sys_time field is left at zero — the renderer doesn't depend on
 * it except for printk continuation, which has its own dedicated
 * test that fills it deliberately.
 */
static void
mkev(struct dp_trace_event *e, int type, int tid)
{
   memset(e, 0, sizeof(*e));
   e->type = type;
   e->tid = tid;
}

/*
 * Look up where a given syscall's `param_idx` parameter ends up in
 * the saved_params area, using the metadata blob. The slot allocator
 * (modules/tracing/tracing.c::alloc_for_fmt0) is greedy — it picks
 * the smallest free slot that fits — so the offset for a given
 * (sys_n, param_idx) is not knowable at test-author time. Reading
 * it from /syst/tracing/metadata makes the test resilient to slot
 * allocator changes.
 *
 * Returns the byte offset on success, -1 if the syscall has no
 * metadata or the param is register-only (no slot).
 */
static int
slot_offset(unsigned sys_n, int param_idx)
{
   const struct tr_wire_syscall *si = tr_get_sys_info(sys_n);

   if (!si || param_idx >= si->n_params || si->slots[param_idx] < 0)
      return -1;

   return tr_fmt_offsets[si->fmt][si->slots[param_idx]];
}

/*
 * Push `e` into the ring, read events back until we hit one with a
 * test-sentinel tid (INJ_TID / INJ_TID2), copy it into `out`. Returns
 * 0 on success, -1 on failure (the caller already called test_fail
 * in that case).
 *
 * Why the loop: cmd_set_enabled is OFF during Tier 2, so the global
 * syscall-trace gate doesn't produce events. trace_printk(), however,
 * is gated only by __tracing_printk_lvl (default 10) — not by the
 * global enabled flag — so a kernel printk from e.g. clock_drift_adj
 * can race in between drain_ring() and our read() and end up in the
 * ring ahead of our injected event. Skip past anything whose tid
 * isn't one of our sentinels (90001/90002, well outside real task
 * and worker-thread tid ranges).
 */
static int
inject_and_read(const char *name,
                const struct dp_trace_event *e,
                struct dp_trace_event *out)
{
   char r[80];

   drain_ring();

   if (cmd_inject_event(e) < 0) {
      snprintf(r, sizeof(r), "cmd_inject_event failed (errno=%d)", errno);
      goto fail;
   }

   for (;;) {

      ssize_t n = read(events_fd, out, sizeof(*out));

      if (n != (ssize_t)sizeof(*out)) {
         snprintf(r, sizeof(r),
                  "read after inject failed (n=%ld, errno=%d)",
                  (long)n, errno);
         goto fail;
      }

      if (out->tid == INJ_TID || out->tid == INJ_TID2)
         return 0;

      /* Kernel-side event raced in — skip and keep reading. */
   }

fail:
   n_failed++;
   /* test_fail clones the reason buffer; we re-emit it here. */
   printf("  [FAIL] %s: %s\n", name, r);
   return -1;
}

static bool
render_contains(const struct dp_trace_event *e, const char *needle)
{
   struct dp_render_ctx rctx = {0};
   char buf[RENDER_BUF_SZ];
   int n;

   n = tr_render_event(e, buf, sizeof(buf), &rctx);

   if (n <= 0)
      return false;

   if (n >= (int)sizeof(buf))
      n = (int)sizeof(buf) - 1;

   buf[n] = 0;

   if (strstr(buf, needle) == NULL) {

      /*
       * Debug: print the render so the test author can see why the
       * assertion failed. Strip ANSI escapes from the dump so the
       * output is readable in QEMU's text-mode console.
       */
      printf("    render: ");
      for (int i = 0; i < n; i++) {

         if (buf[i] == '\x1b') {

            /* Skip a CSI escape like \x1b[31m. */
            while (i < n && buf[i] != 'm' && buf[i] != 'H')
               i++;
            continue;
         }

         putchar(buf[i]);
      }
      putchar('\n');
      return false;
   }

   return true;
}

static void
test_pass(const char *name)
{
   n_passed++;
   printf("  [PASS] %s\n", name);
}

static void
test_fail(const char *name, const char *reason)
{
   n_failed++;
   printf("  [FAIL] %s: %s\n", name, reason);
}

/* -------------------------------- tests ------------------------------ */

/* Event-type coverage 01: sys_enter for SYS_getpid (no params, no
 * saved data — the renderer just emits "getpid"). */
static void
test_inj_sys_enter_getpid(void)
{
   const char *name = "inj_sys_enter_getpid";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_getpid;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (back.type != dp_te_sys_enter) {
      test_fail(name, "type mismatch");
      return;
   }

   if (back.tid != INJ_TID || back.sys_ev.sys != SYS_getpid) {
      test_fail(name, "tid/sys mismatch");
      return;
   }

   if (!render_contains(&back, "getpid")) {
      test_fail(name, "render missing 'getpid'");
      return;
   }

   test_pass(name);
}

/* 02: sys_exit with a non-trivial retval. */
static void
test_inj_sys_exit_retval(void)
{
   const char *name = "inj_sys_exit_retval";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_exit, INJ_TID);
   ev.sys_ev.sys = SYS_getpid;
   ev.sys_ev.retval = 42;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (back.type != dp_te_sys_exit || back.sys_ev.retval != 42) {
      test_fail(name, "type/retval mismatch");
      return;
   }

   if (!render_contains(&back, "42")) {
      test_fail(name, "render missing '42'");
      return;
   }

   test_pass(name);
}

/* 03: sys_exit with -ENOENT — verifies the errno-name decode. */
static void
test_inj_sys_exit_errno(void)
{
   const char *name = "inj_sys_exit_errno";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_exit, INJ_TID);
   ev.sys_ev.sys = SYS_open;
   ev.sys_ev.retval = -ENOENT;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "ENOENT")) {
      test_fail(name, "render missing 'ENOENT'");
      return;
   }

   test_pass(name);
}

/* 04: printk single-line event. */
static void
test_inj_printk_single(void)
{
   const char *name = "inj_printk_single";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_printk, INJ_TID);
   ev.p_ev.level = 5;
   strcpy(ev.p_ev.buf, "hello injected printk\n");

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "hello injected printk")) {
      test_fail(name, "render missing 'hello injected printk'");
      return;
   }

   test_pass(name);
}

/* 05: signal_delivered event with SIGTERM. */
static void
test_inj_signal_delivered(void)
{
   const char *name = "inj_signal_delivered";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_signal_delivered, INJ_TID);
   ev.sig_ev.signum = SIGTERM;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (back.type != dp_te_signal_delivered) {
      test_fail(name, "type mismatch");
      return;
   }

   if (!render_contains(&back, "SIGTERM")) {
      test_fail(name, "render missing 'SIGTERM'");
      return;
   }

   test_pass(name);
}

/* 06: killed event with SIGKILL. */
static void
test_inj_killed(void)
{
   const char *name = "inj_killed";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_killed, INJ_TID);
   ev.sig_ev.signum = SIGKILL;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "SIGKILL")) {
      test_fail(name, "render missing 'SIGKILL'");
      return;
   }

   test_pass(name);
}

/*
 * 07: ptype_signum via sys_kill — args[1] holds signum, args[0] is
 * pid. The renderer reads from the register array (no save).
 */
static void
test_inj_ptype_signum(void)
{
   const char *name = "inj_ptype_signum";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_kill;
   ev.sys_ev.args[0] = 1234;
   ev.sys_ev.args[1] = SIGUSR1;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "SIGUSR1")) {
      test_fail(name, "render missing 'SIGUSR1'");
      return;
   }

   test_pass(name);
}

/*
 * 08: ptype_open_flags — sys_open with O_RDONLY|O_NONBLOCK in args[1].
 * Path slot is intentionally blank; we only assert on the flag.
 */
static void
test_inj_ptype_open_flags(void)
{
   const char *name = "inj_ptype_open_flags";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_open;
   ev.sys_ev.args[1] = O_RDONLY | O_NONBLOCK;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "O_NONBLOCK")) {
      test_fail(name, "render missing 'O_NONBLOCK'");
      return;
   }

   test_pass(name);
}

/* 09: ptype_mmap_prot — mmap2 args[2]. */
static void
test_inj_ptype_mmap_prot(void)
{
   const char *name = "inj_ptype_mmap_prot";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_mmap2;
   ev.sys_ev.args[2] = PROT_READ | PROT_WRITE;
   ev.sys_ev.args[3] = MAP_PRIVATE | MAP_ANONYMOUS;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "PROT_READ")) {
      test_fail(name, "render missing 'PROT_READ'");
      return;
   }

   test_pass(name);
}

/* 10: ptype_mmap_flags — same event, different assertion. */
static void
test_inj_ptype_mmap_flags(void)
{
   const char *name = "inj_ptype_mmap_flags";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_mmap2;
   ev.sys_ev.args[2] = PROT_READ;
   ev.sys_ev.args[3] = MAP_PRIVATE | MAP_ANONYMOUS;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "MAP_PRIVATE")) {
      test_fail(name, "render missing 'MAP_PRIVATE'");
      return;
   }

   if (!render_contains(&back, "MAP_ANONYMOUS")) {
      test_fail(name, "render missing 'MAP_ANONYMOUS'");
      return;
   }

   test_pass(name);
}

/* 11: ptype_whence — lseek args[2]. */
static void
test_inj_ptype_whence(void)
{
   const char *name = "inj_ptype_whence";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS__llseek;
   ev.sys_ev.args[3] = SEEK_END;     /* llseek: whence is args[4] on i386 */
   ev.sys_ev.args[4] = SEEK_END;     /* try both for robustness */

   /*
    * On i386 SYS__llseek's whence position depends on the metadata's
    * slot layout, but the renderer reads from the register-value
    * position the metadata declares. We don't try to be clever —
    * just stuff SEEK_END in a couple of slots so at least one matches.
    * If the renderer ever changes which slot holds whence the test
    * will need updating.
    */

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "SEEK_END")) {
      test_fail(name, "render missing 'SEEK_END'");
      return;
   }

   test_pass(name);
}

/* 12: ptype_sigprocmask_how — args[0]. */
static void
test_inj_ptype_sigprocmask_how(void)
{
   const char *name = "inj_ptype_sigprocmask_how";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_rt_sigprocmask;
   ev.sys_ev.args[0] = SIG_BLOCK;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "SIG_BLOCK")) {
      test_fail(name, "render missing 'SIG_BLOCK'");
      return;
   }

   test_pass(name);
}

/*
 * 13: ptype_path — sys_open with a path string in saved_params slot 0
 * (fmt0 layout: slot 0 starts at offset 0, max 64 bytes). The buffer
 * dump renderer reads up to the slot size and treats it as a string.
 */
static void
test_inj_ptype_path(void)
{
   const char *name = "inj_ptype_path";
   struct dp_trace_event ev, back;

   /* Path is sys_open's first param. */
   int off = slot_offset(SYS_open, 0);

   if (off < 0) {
      test_fail(name, "no slot for SYS_open param 0");
      return;
   }

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_open;
   ev.sys_ev.args[0] = 0xdeadbeef;   /* would-be user ptr */

   const char path[] = "/tmp/injected_path";
   memcpy(&ev.sys_ev.saved_params[off], path, sizeof(path));

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "/tmp/injected_path")) {
      test_fail(name, "render missing path");
      return;
   }

   test_pass(name);
}

/*
 * 14: "<fault>" marker — when copy_from_user fails the kernel writes
 * literal "<fault>" into the slot. The renderer should propagate
 * that string verbatim into the dumped output.
 */
static void
test_inj_fault_marker(void)
{
   const char *name = "inj_fault_marker";
   struct dp_trace_event ev, back;

   int off = slot_offset(SYS_open, 0);

   if (off < 0) {
      test_fail(name, "no slot for SYS_open param 0");
      return;
   }

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_open;
   ev.sys_ev.args[0] = 0xdeadbeef;        /* would-be user ptr */
   memcpy(&ev.sys_ev.saved_params[off], "<fault>", 8);

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "<fault>")) {
      test_fail(name, "render missing '<fault>'");
      return;
   }

   test_pass(name);
}

/*
 * 15: ptype_wstatus (Layer 3a) — waitpid-family. fmt1 layout, slot 0
 * is 128 B; the wstatus int sits at offset 0. WIFEXITED(rc=7) → 0x07.
 */
static void
test_inj_ptype_wstatus(void)
{
   const char *name = "inj_ptype_wstatus";
   struct dp_trace_event ev, back;

   /* wstatus is wait4's param 1 (see tracing_metadata.c). */
   int off = slot_offset(SYS_wait4, 1);

   if (off < 0) {
      test_fail(name, "no slot for SYS_wait4 param 1");
      return;
   }

   mkev(&ev, dp_te_sys_exit, INJ_TID);
   ev.sys_ev.sys = SYS_wait4;
   ev.sys_ev.retval = 1234;       /* pid waited for */
   ev.sys_ev.args[1] = 0xdeadbeef; /* would-be wstatus user ptr */
   /* WIFEXITED with code 7 → low byte 0x00, high byte 0x07. */
   const int wstatus = 7 << 8;
   memcpy(&ev.sys_ev.saved_params[off], &wstatus, sizeof(wstatus));

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "WIFEXITED")) {
      test_fail(name, "render missing 'WIFEXITED'");
      return;
   }

   test_pass(name);
}

/*
 * 16: ptype_ioctl_argp + helper. ioctl with TIOCGWINSZ should drive
 * the renderer into the winsize-struct dump path.
 */
static void
test_inj_ioctl_argp(void)
{
   const char *name = "inj_ioctl_argp";
   struct dp_trace_event ev, back;

   /* argp is ioctl's param 2 (context-dependent on `request`). */
   int off = slot_offset(SYS_ioctl, 2);

   if (off < 0) {
      test_fail(name, "no slot for SYS_ioctl argp");
      return;
   }

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_ioctl;
   ev.sys_ev.args[1] = 0x5413;       /* TIOCGWINSZ */
   ev.sys_ev.args[2] = 0xdeadbeef;   /* would-be argp user ptr */

   /* struct winsize: 4 × unsigned short. */
   const unsigned short ws[4] = { 25, 80, 0, 0 };
   memcpy(&ev.sys_ev.saved_params[off], ws, sizeof(ws));

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "TIOCGWINSZ")) {
      test_fail(name, "render missing 'TIOCGWINSZ'");
      return;
   }

   /* Assert on the struct decode so we catch a regression in
    * tr_dump_ioctl_argp, not just the cmd name in ptype_ioctl_cmd. */
   if (!render_contains(&back, "ws_row = 25")) {
      test_fail(name, "render missing 'ws_row = 25'");
      return;
   }

   test_pass(name);
}

/* 17: ptype_fcntl_cmd renders as F_SETFL when args[1] = 4 (F_SETFL). */
static void
test_inj_fcntl_cmd(void)
{
   const char *name = "inj_fcntl_cmd";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_fcntl64;
   ev.sys_ev.args[1] = 4;                  /* F_SETFL */
   ev.sys_ev.args[2] = O_NONBLOCK;         /* the fcntl_arg */

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (!render_contains(&back, "F_SETFL")) {
      test_fail(name, "render missing 'F_SETFL'");
      return;
   }

   if (!render_contains(&back, "O_NONBLOCK")) {
      test_fail(name, "render missing 'O_NONBLOCK'");
      return;
   }

   test_pass(name);
}

/*
 * 18: unknown sys_n (no metadata) — renderer falls back to a bare
 * name-or-number form. Pick a high syscall number that no Tilck
 * arch implements.
 */
static void
test_inj_unknown_sys_n(void)
{
   const char *name = "inj_unknown_sys_n";
   struct dp_trace_event ev, back;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = 9999;

   if (inject_and_read(name, &ev, &back) < 0)
      return;

   if (back.sys_ev.sys != 9999) {
      test_fail(name, "sys_n round-trip mismatch");
      return;
   }

   /*
    * The renderer prints something like "syscall_9999()" for unknown
    * IDs. Assert just on the number — exact format may vary across
    * future renderer tweaks.
    */
   if (!render_contains(&back, "9999")) {
      test_fail(name, "render missing '9999'");
      return;
   }

   test_pass(name);
}

/*
 * 19: tid round-trip — distinct tids on consecutive injects. Catches
 * a class of "renderer caches the last tid" bugs.
 */
static void
test_inj_tid_roundtrip(void)
{
   const char *name = "inj_tid_roundtrip";
   struct dp_trace_event ev_a, ev_b, back_a, back_b;

   mkev(&ev_a, dp_te_sys_enter, INJ_TID);
   ev_a.sys_ev.sys = SYS_getpid;

   mkev(&ev_b, dp_te_sys_enter, INJ_TID2);
   ev_b.sys_ev.sys = SYS_getpid;

   drain_ring();

   if (cmd_inject_event(&ev_a) < 0 || cmd_inject_event(&ev_b) < 0) {
      test_fail(name, "inject failed");
      return;
   }

   if (read(events_fd, &back_a, sizeof(back_a)) != (ssize_t)sizeof(back_a) ||
       read(events_fd, &back_b, sizeof(back_b)) != (ssize_t)sizeof(back_b))
   {
      test_fail(name, "read failed");
      return;
   }

   if (back_a.tid != INJ_TID || back_b.tid != INJ_TID2) {
      test_fail(name, "tid round-trip mismatch");
      return;
   }

   test_pass(name);
}

/*
 * 20: gate check — disable test mode and confirm inject is rejected
 * with -EPERM. Re-enable before returning so subsequent tests work.
 */
static void
test_inj_gate(void)
{
   const char *name = "inj_gate_eperm";
   struct dp_trace_event ev;

   mkev(&ev, dp_te_sys_enter, INJ_TID);
   ev.sys_ev.sys = SYS_getpid;

   cmd_set_test_mode(0);
   long rc = cmd_inject_event(&ev);
   cmd_set_test_mode(1);

   if (rc >= 0) {
      test_fail(name, "inject succeeded with test mode off");
      return;
   }

   if (errno != EPERM) {
      char r[80];
      snprintf(r, sizeof(r), "expected EPERM, got errno=%d", errno);
      test_fail(name, r);
      return;
   }

   test_pass(name);
}

/* ----------------------------- drivers -------------------------------- */

int
tr_run_tier2_tests(void)
{
   if (tr_meta_init() < 0) {
      fprintf(stderr, "tracer --test: tr_meta_init failed\n");
      return 2;
   }

   events_fd = open(EVENTS_PATH, O_RDONLY | O_NONBLOCK);

   if (events_fd < 0) {
      fprintf(stderr,
              "tracer --test: open %s failed (errno=%d)\n",
              EVENTS_PATH, errno);
      return 2;
   }

   if (cmd_set_test_mode(1) < 0) {
      fprintf(stderr,
              "tracer --test: cmd_set_test_mode failed (errno=%d) — "
              "is MOD_tracing built in?\n", errno);
      close(events_fd);
      return 2;
   }

   printf("tracer --test: Tier 2 (event injection)\n");

   test_inj_sys_enter_getpid();
   test_inj_sys_exit_retval();
   test_inj_sys_exit_errno();
   test_inj_printk_single();
   test_inj_signal_delivered();
   test_inj_killed();
   test_inj_ptype_signum();
   test_inj_ptype_open_flags();
   test_inj_ptype_mmap_prot();
   test_inj_ptype_mmap_flags();
   test_inj_ptype_whence();
   test_inj_ptype_sigprocmask_how();
   test_inj_ptype_path();
   test_inj_fault_marker();
   test_inj_ptype_wstatus();
   test_inj_ioctl_argp();
   test_inj_fcntl_cmd();
   test_inj_unknown_sys_n();
   test_inj_tid_roundtrip();
   test_inj_gate();

   cmd_set_test_mode(0);
   close(events_fd);
   events_fd = -1;

   printf("\n%d/%d PASS\n", n_passed, n_passed + n_failed);
   return n_failed == 0 ? 0 : 1;
}

/*
 * --stress: inject STRESS_NEVENTS events into the ring, no draining
 * between. The ring will overrun (its capacity is much smaller than
 * 10k); the surviving events are some contiguous tail. Drain and
 * check each one round-trips correctly: type, tid in the expected
 * set, and sys_n equal to the cycling counter we encoded into the
 * arg array.
 *
 * What this proves:
 *   - ringbuf_write_elem under back-pressure doesn't corrupt the
 *     event payload.
 *   - drain after stress yields events with valid (sys_n, tid)
 *     pairs from the injection sequence.
 *   - the renderer survives every surviving event (no crashes /
 *     no negative-length returns).
 */
int
tr_run_stress_test(void)
{
   if (tr_meta_init() < 0) {
      fprintf(stderr, "tracer --test: tr_meta_init failed\n");
      return 2;
   }

   events_fd = open(EVENTS_PATH, O_RDONLY | O_NONBLOCK);

   if (events_fd < 0) {
      fprintf(stderr,
              "tracer --test: open %s failed (errno=%d)\n",
              EVENTS_PATH, errno);
      return 2;
   }

   if (cmd_set_test_mode(1) < 0) {
      fprintf(stderr,
              "tracer --test: cmd_set_test_mode failed (errno=%d)\n",
              errno);
      close(events_fd);
      return 2;
   }

   printf("tracer --test --stress: injecting %d events\n", STRESS_NEVENTS);

   drain_ring();

   for (int i = 0; i < STRESS_NEVENTS; i++) {

      struct dp_trace_event ev;
      mkev(&ev, dp_te_sys_enter, INJ_TID);
      ev.sys_ev.sys = SYS_getpid;
      ev.sys_ev.args[0] = (unsigned long)i;   /* sequence counter */

      if (cmd_inject_event(&ev) < 0) {
         fprintf(stderr, "inject failed at i=%d errno=%d\n", i, errno);
         cmd_set_test_mode(0);
         close(events_fd);
         return 1;
      }
   }

   /* Drain and validate. */
   int n_read = 0;
   int n_bad = 0;
   int last_counter = -1;
   bool monotone = true;
   char render_buf[RENDER_BUF_SZ];

   while (1) {

      struct dp_trace_event back;
      ssize_t n = read(events_fd, &back, sizeof(back));

      if (n != (ssize_t)sizeof(back))
         break;

      n_read++;

      if (back.type != dp_te_sys_enter ||
          back.tid != INJ_TID ||
          back.sys_ev.sys != SYS_getpid)
      {
         n_bad++;
         continue;
      }

      int counter = (int)back.sys_ev.args[0];

      if (counter < 0 || counter >= STRESS_NEVENTS)
         n_bad++;

      if (counter <= last_counter)
         monotone = false;

      last_counter = counter;

      /*
       * Render every event and discard the output — the goal is to
       * surface any renderer crash, not to inspect the text.
       */
      struct dp_render_ctx rctx = {0};
      tr_render_event(&back, render_buf, sizeof(render_buf), &rctx);
   }

   cmd_set_test_mode(0);
   close(events_fd);
   events_fd = -1;

   printf("  drained %d events (%d bad, monotone=%s)\n",
          n_read, n_bad, monotone ? "yes" : "no");

   if (n_read == 0) {
      printf("  [FAIL] no events drained\n");
      return 1;
   }

   if (n_bad > 0) {
      printf("  [FAIL] %d events had wrong type/tid/sys_n\n", n_bad);
      return 1;
   }

   if (!monotone) {
      printf("  [FAIL] event counter not monotonically increasing\n");
      return 1;
   }

   printf("  [PASS] stress test\n");
   return 0;
}

#else  /* !__i386__ */

#include <stdio.h>
#include "tr.h"

int
tr_run_tier2_tests(void)
{
   printf("tracer --test: Tier 2 event-injection tests are i386-only\n");
   return 0;
}

int
tr_run_stress_test(void)
{
   printf("tracer --test --stress is i386-only\n");
   return 0;
}

#endif /* __i386__ */
