/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/syscalls.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
   int rc = -1;

   if (argc > 1) {

      if (!strcmp(argv[1], "-t")) {

         rc = syscall(TILCK_CMD_SYSCALL, TILCK_CMD_TRACING_TOOL);

         if (rc < 0)
            printf("Tracing not compiled-in\n");

      } else {

         printf("Unknown option\n");
      }

      return rc != 0;
   }

   rc = syscall(TILCK_CMD_SYSCALL, TILCK_CMD_DEBUG_PANEL);

   if (rc < 0)
      printf("Debug panel not compiled-in\n");

   return rc != 0;
}
