/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Entry point for the `tracer` userspace app.
 *
 * The tracer used to be a sub-mode of `dp` (invoked via the
 * `tracer` symlink or `dp -t`); it lives in its own binary now so
 * the dp panel doesn't drag in ~1.5 KLOC of trace-rendering code
 * and the tracer is usable on builds where MOD_debugpanel is off.
 *
 * Per-task `.traced` flags + the syscall filter live in the kernel
 * (MOD_tracing); the panel and the tracer just talk to that state
 * via TILCK_CMD_DP_TASK_SET_TRACED / TRACE_SET_FILTER. So Ctrl+T
 * from the dp Tasks panel just fork+execs this binary — no state
 * needs to flow through argv.
 *
 *   tracer           — full-screen interactive UI (dp_run_tracer)
 *   tracer --test    — non-interactive self-test mode (see test_live.c)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tr.h"

int main(int argc, char **argv)
{
   if (!getenv("TILCK")) {
      printf("ERROR: the tracer exists only on Tilck!\n");
      return 1;
   }

   if (argc > 1 && !strcmp(argv[1], "--test"))
      return tr_run_tier1_tests();

   return dp_run_tracer();
}
