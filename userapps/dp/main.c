/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/syscalls.h>

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include "dp_int.h"

static int tracer_tool(int argc, char **argv)
{
   if (argc > 0) {
      printf("ERROR: unknown option '%s'\n", argv[0]);
      return 1;
   }

   /*
    * Tracer mode is still served by the in-kernel TUI for now; Phase 8
    * replaces this with a userspace tracer panel that reads
    * /syst/tracing/events.
    */
   int rc = syscall(TILCK_CMD_SYSCALL, TILCK_CMD_TRACING_TOOL);

   if (rc < 0)
      printf("ERROR: tracing not compiled-in\n");

   return rc;
}

static int ps_tool(int argc, char **argv)
{
   if (argc > 0) {
      printf("ERROR: unknown option '%s'\n", argv[0]);
      return 1;
   }

   /*
    * ps mode renders the same task table as the Tasks panel, but
    * once and as plain text. Implementation lives in screen_tasks.c.
    */
   return dp_run_ps();
}

static int debug_panel(int argc, char **argv)
{
   if (argc > 0) {

      if (!strcmp(argv[0], "-t"))
         return tracer_tool(argc-1, argv+1);

      printf("ERROR: unknown option '%s'\n", argv[0]);
      return 1;
   }

   /*
    * Plain `dp` runs the userspace TUI: it pulls kernel state via the
    * TILCK_CMD_DP_GET_* sub-commands and the /syst/* sysfs trees, then
    * renders panels with the local termutil/dp_input infrastructure.
    */
   return dp_run_panel();
}

int main(int argc, char **argv)
{
   int rc = -1;
   char *prog_name;

   if (argc == 0) {
      printf("ERROR: argc is 0\n");
      return 1;
   }

   if (!getenv("TILCK")) {

      printf("ERROR: the debug panel exists only on Tilck!\n");
      return 1;
   }

   prog_name = basename(argv[0]);

   if (!strcmp(prog_name, "dp")) {

      rc = debug_panel(argc-1, argv+1);

   } else if (!strcmp(prog_name, "tracer")) {

      rc = tracer_tool(argc-1, argv+1);

   } else if (!strcmp(prog_name, "ps")) {

      rc = ps_tool(argc-1, argv+1);

   } else {

      printf("Unknown '%s' tool\n", prog_name);
      rc = -1;
   }

   return rc != 0;
}
