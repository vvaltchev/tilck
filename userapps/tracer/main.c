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
 *   tracer                — full-screen interactive UI (dp_run_tracer)
 *   tracer --test         — Tier 1 + Tier 2 self-tests
 *   tracer --test --stress
 *                         — ring-buffer overrun stress test
 *   tracer -h, --help     — show usage and exit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tr.h"

static bool is_help_opt(const char *s)
{
   return !strcmp(s, "-h") || !strcmp(s, "--help");
}

static void show_help_and_exit(void)
{
   printf("Tilck tracer -- kernel syscall + printk tracer.\n\n");
   printf("Usage:\n");
   printf("  tracer                  Run the interactive tracer (banner,\n");
   printf("                          live event stream, key dispatch).\n");
   printf("                          'h' shows in-app help; 'q' exits.\n");
   printf("  tracer --test           Run the self-tests:\n");
   printf("                            Tier 1 -- live syscall integration\n");
   printf("                            Tier 2 -- synthetic event injection\n");
   printf("                          Exits 0 if all PASS, 1 otherwise.\n");
   printf("  tracer --test --stress  Inject 10000 events into the ring\n");
   printf("                          buffer and verify the surviving\n");
   printf("                          events round-trip correctly.\n");
   printf("  tracer -h, --help       Show this help and exit.\n");
   exit(0);
}

int main(int argc, char **argv)
{
   if (!getenv("TILCK")) {
      printf("ERROR: the tracer exists only on Tilck!\n");
      return 1;
   }

   if (argc > 1 && is_help_opt(argv[1]))
      show_help_and_exit();

   if (argc > 1 && !strcmp(argv[1], "--test")) {

      if (argc > 2 && !strcmp(argv[2], "--stress"))
         return tr_run_stress_test();

      int rc1 = tr_run_tier1_tests();
      int rc2 = tr_run_tier2_tests();
      return rc1 == 0 && rc2 == 0 ? 0 : 1;
   }

   return dp_run_tracer();
}
