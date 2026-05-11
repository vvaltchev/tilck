/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * `tracer --test` — Tier 1 live-syscall integration tests.
 *
 * The kernel refuses TILCK_CMD_DP_TASK_SET_TRACED for the calling
 * task itself (tracing_cmd.c:87 — a guard against a tracer observing
 * its own observation syscalls), so every test forks a child, marks
 * the child as traced, issues one syscall in the child, then waits
 * and drains /syst/tracing/events in the parent.
 *
 * The harness `run_traced` does the fork + pipe handshake. The child
 * writes a "ready" byte before its action and blocks on a second
 * pipe; the parent waits for the ready byte (so we know the child
 * has reached the sync read), THEN sets the filter / marks the
 * child as traced / enables tracing / unblocks the child. The
 * sync-read at the child end runs untraced at ENTER (no event), and
 * its EXIT only emits an event when the filter happens to include
 * SYS_read (test_read). For that one case we disambiguate the sync
 * read from the action read by buffer size — see test_read.
 *
 * The set of cases is deliberately broad: one per ptype family that
 * has a save callback or a non-trivial renderer. Failures print a
 * short reason and continue with the next case so we get full
 * coverage per run.
 *
 * Tier 2 (event injection — a kernel TILCK_CMD that lets userspace
 * push synthetic events into the ring) lands in a follow-up commit;
 * this file deliberately does not anticipate it.
 *
 * i386-only: the test syscalls below reference SYS_open / SYS_fcntl64
 * / SYS__llseek / SYS_mmap2 / SYS_sigprocmask / SYS_poll / SYS_dup2
 * — these are the i386 syscall numbers. On x86_64 and riscv64 musl
 * those constants don't exist (the kernel uses openat / fcntl /
 * lseek / mmap / rt_sigprocmask / ppoll / dup3). Rather than
 * conditionally renaming every test, the whole file is wrapped in
 * `#ifdef __i386__`; on other arches tr_run_tier1_tests is a stub.
 * Tier 1 coverage on x86_64 / riscv64 is a future refactor.
 */

#ifdef __i386__

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "tr.h"

#define EVENTS_PATH       "/syst/tracing/events"
#define MAX_CAPTURED      64
#define RENDER_BUF_SZ     1024
#define READ_BUF_SZ       64

/* ----------------------- TILCK_CMD wrappers -------------------------- */

static long
cmd_set_filter(const char *expr)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_FILTER,
                  (long)expr, 0L, 0L, 0L);
}

static long
cmd_set_enabled(int enabled)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_ENABLED,
                  (long)enabled, 0L, 0L, 0L);
}

static long
cmd_set_task_traced(int tid, int enabled)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TASK_SET_TRACED,
                  (long)tid, (long)enabled, 0L, 0L);
}

static long
cmd_set_force_exp_block(int v)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_FORCE_EXP_BLOCK,
                  (long)v, 0L, 0L, 0L);
}

/* ------------------------------ harness ------------------------------ */

struct capture {
   struct dp_trace_event events[MAX_CAPTURED];
   int count;
   int child_tid;          /* the tid we expect events from */
};

typedef void (*test_action_fn)(void *ctx);

static int events_fd = -1;
static int n_passed;
static int n_failed;

/* Drain anything left in the ring before each test. */
static void
drain_ring(void)
{
   struct dp_trace_event ev;

   while (read(events_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) { }
}

/*
 * Read all available events into cap->events[]. Stops at MAX_CAPTURED
 * or on EAGAIN/EOF. Called by parent after the traced child exits.
 */
static void
drain_into(struct capture *cap)
{
   cap->count = 0;

   while (cap->count < MAX_CAPTURED) {

      ssize_t n = read(events_fd, &cap->events[cap->count],
                       sizeof(struct dp_trace_event));

      if (n != (ssize_t)sizeof(struct dp_trace_event))
         break;

      cap->count++;
   }
}

/*
 * Fork, run `action(ctx)` in the child under tracing scoped to that
 * child + the given filter, drain the events into cap. Returns 0 on
 * success, -1 on harness failure.
 *
 * Two pipes:
 *   ready[0..1] — child writes 'r' before sync read (so parent knows
 *                 the child has reached the blocked-in-read state).
 *   go[0..1]    — parent writes 'g' to release the child once
 *                 tracing is set up.
 */
static int
run_traced(test_action_fn action,
           void *ctx,
           const char *filter,
           struct capture *cap)
{
   int ready[2], go[2];
   pid_t cpid;
   char b;

   if (pipe(ready) < 0)
      return -1;

   if (pipe(go) < 0) {
      close(ready[0]);
      close(ready[1]);
      return -1;
   }

   cpid = fork();

   if (cpid < 0) {
      close(ready[0]);
      close(ready[1]);
      close(go[0]);
      close(go[1]);
      return -1;
   }

   if (cpid == 0) {

      /* child */
      close(ready[0]);
      close(go[1]);

      write(ready[1], "r", 1);    /* signal "I'm about to block" */
      read(go[0], &b, 1);         /* wait for parent's "go" */

      action(ctx);
      _exit(0);
   }

   /* parent */
   close(ready[1]);
   close(go[0]);

   if (read(ready[0], &b, 1) != 1) {
      close(ready[0]);
      close(go[1]);
      waitpid(cpid, NULL, 0);
      return -1;
   }

   /*
    * Child has emitted its ready byte; it's about to enter (or is
    * already in) the blocking read on `go`. Set up tracing now.
    * Drain anything leftover in the ring first so we only see this
    * test's events.
    */
   drain_ring();
   cap->child_tid = cpid;

   cmd_set_filter(filter);
   cmd_set_task_traced(cpid, 1);
   cmd_set_enabled(1);

   write(go[1], "g", 1);

   while (waitpid(cpid, NULL, 0) < 0 && errno == EINTR) { }

   cmd_set_enabled(0);
   cmd_set_task_traced(cpid, 0);

   close(ready[0]);
   close(go[1]);

   drain_into(cap);
   return 0;
}

static const struct dp_trace_event *
find_event(const struct capture *cap, int type, unsigned sys_n)
{
   for (int i = 0; i < cap->count; i++) {

      const struct dp_trace_event *e = &cap->events[i];

      if (e->type == type &&
          e->tid == cap->child_tid &&
          e->sys_ev.sys == sys_n)
      {
         return e;
      }
   }

   return NULL;
}

/*
 * Like find_event but also requires args[arg_idx] == arg_value. Used
 * to disambiguate the sync read from the action read in test_read.
 */
static const struct dp_trace_event *
find_event_arg(const struct capture *cap,
               int type,
               unsigned sys_n,
               int arg_idx,
               unsigned long arg_value)
{
   for (int i = 0; i < cap->count; i++) {

      const struct dp_trace_event *e = &cap->events[i];

      if (e->type == type &&
          e->tid == cap->child_tid &&
          e->sys_ev.sys == sys_n &&
          e->sys_ev.args[arg_idx] == arg_value)
      {
         return e;
      }
   }

   return NULL;
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
   return strstr(buf, needle) != NULL;
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

/* 01. getpid — baseline no-arg syscall. */

static void
act_getpid(void *unused)
{
   getpid();
}

static void
test_getpid(void)
{
   const char *name = "getpid";
   const struct dp_trace_event *e;
   struct capture cap;

   if (run_traced(act_getpid, NULL, "getpid", &cap) < 0) {
      test_fail(name, "harness failed");
      return;
   }

   e = find_event(&cap, dp_te_sys_enter, SYS_getpid);

   if (!e) {
      test_fail(name, "no sys_enter for SYS_getpid");
      return;
   }

   if (!render_contains(e, "getpid")) {
      test_fail(name, "render missing 'getpid'");
      return;
   }

   test_pass(name);
}

/*
 * 02. open + close — path (ptype_path) + flags (ptype_open_flags).
 * Open with O_NONBLOCK so the flags bitmask renderer has a non-zero
 * bit to symbolize (O_RDONLY alone is 0 — produces an empty bitmask).
 */

static void
act_open_close(void *unused)
{
   int fd;

   fd = open("/etc/passwd", O_RDONLY | O_NONBLOCK);

   if (fd >= 0)
      close(fd);
}

static void
test_open_close(void)
{
   const char *name = "open+close";
   const struct dp_trace_event *e;
   struct capture cap;

   if (run_traced(act_open_close, NULL, "open,openat,close", &cap) < 0) {
      test_fail(name, "harness failed");
      return;
   }

   e = find_event(&cap, dp_te_sys_enter, SYS_open);

   if (!e) {
      test_fail(name, "no sys_enter for SYS_open");
      return;
   }

   if (!render_contains(e, "/etc/passwd")) {
      test_fail(name, "open render missing '/etc/passwd'");
      return;
   }

   if (!render_contains(e, "O_NONBLOCK")) {
      test_fail(name, "open render missing 'O_NONBLOCK'");
      return;
   }

   if (!find_event(&cap, dp_te_sys_enter, SYS_close)) {
      test_fail(name, "no sys_enter for SYS_close");
      return;
   }

   test_pass(name);
}

/* 03. read — ptype_buffer with helper "count". */

static void
act_read(void *ctx)
{
   int fd = *(int *)ctx;
   char buf[READ_BUF_SZ];

   read(fd, buf, sizeof(buf));
}

static void
test_read(void)
{
   const char *name = "read";
   const struct dp_trace_event *e;
   struct capture cap;
   int fd;

   fd = open("/etc/passwd", O_RDONLY);

   if (fd < 0) {
      test_fail(name, "setup: open failed");
      return;
   }

   if (run_traced(act_read, &fd, "read", &cap) < 0) {
      close(fd);
      test_fail(name, "harness failed");
      return;
   }

   close(fd);

   /*
    * Filter is "read", so the child's sync-pipe read MAY also emit a
    * sys_exit event (no enter, since tracing wasn't on at the sync
    * read's start). Disambiguate by buffer size — sync read is 1 byte,
    * action read is READ_BUF_SZ.
    */
   e = find_event_arg(&cap, dp_te_sys_enter, SYS_read, 2, READ_BUF_SZ);

   if (!e) {
      test_fail(name, "no sys_enter for SYS_read (count=64)");
      return;
   }

   test_pass(name);
}

/* 04. write — ptype_buffer in IN direction. */

static void
act_write(void *ctx)
{
   int fd = *(int *)ctx;

   write(fd, "hello", 5);
}

static void
test_write(void)
{
   const char *name = "write";
   struct capture cap;
   int pipefd[2];

   if (pipe(pipefd) < 0) {
      test_fail(name, "setup: pipe failed");
      return;
   }

   if (run_traced(act_write, &pipefd[1], "write", &cap) < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      test_fail(name, "harness failed");
      return;
   }

   close(pipefd[0]);
   close(pipefd[1]);

   if (!find_event(&cap, dp_te_sys_enter, SYS_write)) {
      test_fail(name, "no sys_enter for SYS_write");
      return;
   }

   test_pass(name);
}

/*
 * 05. fcntl F_GETFL — regression test for the slot_size=0 + save!=NULL
 * panic we fixed earlier.
 */

static void
act_fcntl_getfl(void *ctx)
{
   int fd = *(int *)ctx;

   fcntl(fd, F_GETFL);
}

static void
test_fcntl_getfl(void)
{
   const char *name = "fcntl_getfl";
   const struct dp_trace_event *e;
   struct capture cap;
   int fd;

   fd = open("/etc/passwd", O_RDONLY);

   if (fd < 0) {
      test_fail(name, "setup: open failed");
      return;
   }

   if (run_traced(act_fcntl_getfl, &fd, "fcntl,fcntl64", &cap) < 0) {
      close(fd);
      test_fail(name, "harness failed");
      return;
   }

   close(fd);

   e = find_event(&cap, dp_te_sys_enter, SYS_fcntl64);

   if (!e)
      e = find_event(&cap, dp_te_sys_enter, SYS_fcntl);

   if (!e) {
      test_fail(name, "no sys_enter for fcntl/fcntl64");
      return;
   }

   if (!render_contains(e, "F_GETFL")) {
      test_fail(name, "render missing 'F_GETFL'");
      return;
   }

   test_pass(name);
}

/* 06. fcntl F_SETFL — symbolic O_NONBLOCK render via ptype_fcntl_arg. */

static void
act_fcntl_setfl(void *ctx)
{
   int fd = *(int *)ctx;

   fcntl(fd, F_SETFL, O_NONBLOCK);
}

static void
test_fcntl_setfl(void)
{
   const char *name = "fcntl_setfl";
   const struct dp_trace_event *e;
   struct capture cap;
   int pipefd[2];

   if (pipe(pipefd) < 0) {
      test_fail(name, "setup: pipe failed");
      return;
   }

   if (run_traced(act_fcntl_setfl, &pipefd[0], "fcntl,fcntl64", &cap) < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      test_fail(name, "harness failed");
      return;
   }

   close(pipefd[0]);
   close(pipefd[1]);

   e = find_event(&cap, dp_te_sys_enter, SYS_fcntl64);

   if (!e)
      e = find_event(&cap, dp_te_sys_enter, SYS_fcntl);

   if (!e) {
      test_fail(name, "no sys_enter for fcntl/fcntl64");
      return;
   }

   if (!render_contains(e, "F_SETFL")) {
      test_fail(name, "render missing 'F_SETFL'");
      return;
   }

   if (!render_contains(e, "O_NONBLOCK")) {
      test_fail(name, "render missing 'O_NONBLOCK'");
      return;
   }

   test_pass(name);
}

/* 07. lseek — ptype_whence symbolic (SEEK_END). */

static void
act_lseek(void *ctx)
{
   int fd = *(int *)ctx;

   lseek(fd, 0, SEEK_END);
}

static void
test_lseek(void)
{
   const char *name = "lseek";
   const struct dp_trace_event *e;
   struct capture cap;
   int fd;

   fd = open("/etc/passwd", O_RDONLY);

   if (fd < 0) {
      test_fail(name, "setup: open failed");
      return;
   }

   if (run_traced(act_lseek, &fd, "lseek,llseek", &cap) < 0) {
      close(fd);
      test_fail(name, "harness failed");
      return;
   }

   close(fd);

   e = find_event(&cap, dp_te_sys_enter, SYS__llseek);

   if (!e)
      e = find_event(&cap, dp_te_sys_enter, SYS_lseek);

   if (!e) {
      test_fail(name, "no sys_enter for lseek/_llseek");
      return;
   }

   if (!render_contains(e, "SEEK_END")) {
      test_fail(name, "render missing 'SEEK_END'");
      return;
   }

   test_pass(name);
}

/* 08. mmap — bitmask render of prot + flags. */

static void
act_mmap(void *unused)
{
   void *p;

   p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

   if (p != MAP_FAILED)
      munmap(p, 4096);
}

static void
test_mmap(void)
{
   const char *name = "mmap";
   const struct dp_trace_event *e;
   struct capture cap;

   if (run_traced(act_mmap, NULL, "mmap*", &cap) < 0) {
      test_fail(name, "harness failed");
      return;
   }

   e = find_event(&cap, dp_te_sys_enter, SYS_mmap2);

   if (!e)
      e = find_event(&cap, dp_te_sys_enter, SYS_mmap);

   if (!e) {
      test_fail(name, "no sys_enter for mmap/mmap2");
      return;
   }

   if (!render_contains(e, "PROT_READ")) {
      test_fail(name, "render missing 'PROT_READ'");
      return;
   }

   if (!render_contains(e, "MAP_PRIVATE")) {
      test_fail(name, "render missing 'MAP_PRIVATE'");
      return;
   }

   test_pass(name);
}

/* 09. munmap — addr + length. */

static void
act_munmap(void *ctx)
{
   void *p = *(void **)ctx;

   munmap(p, 4096);
}

static void
test_munmap(void)
{
   const char *name = "munmap";
   struct capture cap;
   void *p;

   p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

   if (p == MAP_FAILED) {
      test_fail(name, "setup: mmap failed");
      return;
   }

   if (run_traced(act_munmap, &p, "munmap", &cap) < 0) {
      munmap(p, 4096);
      test_fail(name, "harness failed");
      return;
   }

   if (!find_event(&cap, dp_te_sys_enter, SYS_munmap)) {
      test_fail(name, "no sys_enter for SYS_munmap");
      return;
   }

   test_pass(name);
}

/* 10. ioctl TIOCGWINSZ — ptype_ioctl_argp context-dispatch via cmd. */

static void
act_ioctl_winsize(void *unused)
{
   struct winsize ws;

   ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
}

static void
test_ioctl_tiocgwinsz(void)
{
   const char *name = "ioctl_tiocgwinsz";
   const struct dp_trace_event *e;
   struct capture cap;

   if (run_traced(act_ioctl_winsize, NULL, "ioctl", &cap) < 0) {
      test_fail(name, "harness failed");
      return;
   }

   e = find_event(&cap, dp_te_sys_enter, SYS_ioctl);

   if (!e) {
      test_fail(name, "no sys_enter for SYS_ioctl");
      return;
   }

   if (!render_contains(e, "TIOCGWINSZ")) {
      test_fail(name, "render missing 'TIOCGWINSZ'");
      return;
   }

   test_pass(name);
}

/* 11. kill(getpid(), SIGCONT) — ptype_signum render. */

static void
act_kill_self(void *unused)
{
   kill(getpid(), SIGCONT);
}

static void
test_kill_self(void)
{
   const char *name = "kill_self_sigcont";
   const struct dp_trace_event *e;
   struct capture cap;

   if (run_traced(act_kill_self, NULL, "kill", &cap) < 0) {
      test_fail(name, "harness failed");
      return;
   }

   e = find_event(&cap, dp_te_sys_enter, SYS_kill);

   if (!e) {
      test_fail(name, "no sys_enter for SYS_kill");
      return;
   }

   if (!render_contains(e, "SIGCONT")) {
      test_fail(name, "render missing 'SIGCONT'");
      return;
   }

   test_pass(name);
}

/* 12. rt_sigprocmask — ptype_sigprocmask_how symbolic. */

static void
act_rt_sigprocmask(void *unused)
{
   /*
    * Direct syscall — libc may wrap rt_sigprocmask in ways that vary
    * across versions; the raw syscall keeps the test predictable.
    */
   syscall(SYS_rt_sigprocmask, SIG_BLOCK, NULL, NULL, sizeof(sigset_t));
}

static void
test_rt_sigprocmask(void)
{
   const char *name = "rt_sigprocmask";
   const struct dp_trace_event *e;
   struct capture cap;

   if (run_traced(act_rt_sigprocmask, NULL,
                  "rt_sigprocmask,sigprocmask", &cap) < 0)
   {
      test_fail(name, "harness failed");
      return;
   }

   e = find_event(&cap, dp_te_sys_enter, SYS_rt_sigprocmask);

   if (!e)
      e = find_event(&cap, dp_te_sys_enter, SYS_sigprocmask);

   if (!e) {
      test_fail(name, "no sys_enter for rt_sigprocmask/sigprocmask");
      return;
   }

   if (!render_contains(e, "SIG_BLOCK")) {
      test_fail(name, "render missing 'SIG_BLOCK'");
      return;
   }

   test_pass(name);
}

/* 13. poll — pollfd struct + nfds + timeout. */

static void
act_poll(void *ctx)
{
   int fd = *(int *)ctx;
   struct pollfd pfd = { .fd = fd, .events = POLLIN };

   poll(&pfd, 1, 0);
}

static void
test_poll(void)
{
   const char *name = "poll";
   struct capture cap;
   int pipefd[2];

   if (pipe(pipefd) < 0) {
      test_fail(name, "setup: pipe failed");
      return;
   }

   if (run_traced(act_poll, &pipefd[0], "poll", &cap) < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      test_fail(name, "harness failed");
      return;
   }

   close(pipefd[0]);
   close(pipefd[1]);

   if (!find_event(&cap, dp_te_sys_enter, SYS_poll)) {
      test_fail(name, "no sys_enter for SYS_poll");
      return;
   }

   test_pass(name);
}

/* 14. chdir — ptype_path render. */

static void
act_chdir(void *unused)
{
   chdir("/etc");
}

static void
test_chdir(void)
{
   const char *name = "chdir";
   const struct dp_trace_event *e;
   struct capture cap;

   if (run_traced(act_chdir, NULL, "chdir", &cap) < 0) {
      test_fail(name, "harness failed");
      return;
   }

   e = find_event(&cap, dp_te_sys_enter, SYS_chdir);

   if (!e) {
      test_fail(name, "no sys_enter for SYS_chdir");
      return;
   }

   if (!render_contains(e, "/etc")) {
      test_fail(name, "render missing '/etc'");
      return;
   }

   test_pass(name);
}

/* 15. dup2 — fd pair (oldfd, newfd). */

static void
act_dup2(void *ctx)
{
   int fd = *(int *)ctx;
   int n = dup2(fd, 7);

   if (n >= 0 && n != fd)
      close(n);
}

static void
test_dup2(void)
{
   const char *name = "dup2";
   struct capture cap;
   int fd;

   fd = open("/etc/passwd", O_RDONLY);

   if (fd < 0) {
      test_fail(name, "setup: open failed");
      return;
   }

   if (run_traced(act_dup2, &fd, "dup2", &cap) < 0) {
      close(fd);
      test_fail(name, "harness failed");
      return;
   }

   close(fd);

   if (!find_event(&cap, dp_te_sys_enter, SYS_dup2)) {
      test_fail(name, "no sys_enter for SYS_dup2");
      return;
   }

   test_pass(name);
}

/* ----------------------------- driver -------------------------------- */

int
tr_run_tier1_tests(void)
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

   /*
    * Force ENTER+EXIT for every traced syscall. By default the kernel
    * may suppress the ENTER event for non-blocking syscalls. ENTER
    * carries the IN-direction param previews we want to assert on, so
    * the test pipeline insists on both halves.
    */
   cmd_set_force_exp_block(1);

   printf("tracer --test: Tier 1 (live syscalls)\n");

   test_getpid();
   test_open_close();
   test_read();
   test_write();
   test_fcntl_getfl();
   test_fcntl_setfl();
   test_lseek();
   test_mmap();
   test_munmap();
   test_ioctl_tiocgwinsz();
   test_kill_self();
   test_rt_sigprocmask();
   test_poll();
   test_chdir();
   test_dup2();

   cmd_set_force_exp_block(0);
   close(events_fd);
   events_fd = -1;

   printf("\n%d/%d PASS\n", n_passed, n_passed + n_failed);
   return n_failed == 0 ? 0 : 1;
}

#else  /* !__i386__ */

#include <stdio.h>
#include "tr.h"

int
tr_run_tier1_tests(void)
{
   printf("tracer --test: Tier 1 live-syscall tests are i386-only\n");
   printf("               (the test syscalls use i386 syscall numbers\n");
   printf("                that don't exist in x86_64 / riscv64 musl)\n");
   return 0;
}

#endif /* __i386__ */
