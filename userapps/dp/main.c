/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/syscalls.h>

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include "dp_int.h"

static bool is_help_opt(const char *s)
{
   return !strcmp(s, "-h") || !strcmp(s, "--help");
}

static void show_dp_help_and_exit(void)
{
   printf("Tilck debug panel -- interactive kernel state inspector.\n\n");
   printf("Usage:\n");
   printf("  dp                   Run the interactive panel (tabs for\n");
   printf("                       tasks, heaps, IRQs, memory map, etc.).\n");
   printf("                       Switch tabs with the digit keys; ENTER\n");
   printf("                       enters select mode on Tasks; Ctrl+T\n");
   printf("                       launches the tracer; 'q' exits.\n");
   printf("  dp -h, --help        Show this help and exit.\n");
   exit(0);
}

static void show_ps_help_and_exit(void)
{
   printf("Tilck ps -- plain-text dump of the kernel task table.\n\n");
   printf("Usage:\n");
   printf("  ps                   Print the task table once and exit.\n");
   printf("                       Same columns as dp's Tasks panel.\n");
   printf("  ps -h, --help        Show this help and exit.\n");
   exit(0);
}

static int ps_tool(int argc, char **argv)
{
   if (argc > 0) {

      if (is_help_opt(argv[0]))
         show_ps_help_and_exit();

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

      if (is_help_opt(argv[0]))
         show_dp_help_and_exit();

      printf("ERROR: unknown option '%s'\n", argv[0]);
      return 1;
   }

   /*
    * Plain `dp` runs the userspace TUI: it pulls kernel state via
    * the TILCK_CMD_DP_GET_  sub-commands and the /syst/  sysfs trees,
    * then renders panels with the local termutil/dp_input layer.
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

   } else if (!strcmp(prog_name, "ps")) {

      rc = ps_tool(argc-1, argv+1);

   } else {

      printf("Unknown '%s' tool\n", prog_name);
      rc = -1;
   }

   return rc != 0;
}
