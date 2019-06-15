/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <dirent.h>

#include "devshell.h"
#include "sysenter.h"
#include "test_common.h"

void create_test_file1(void);
void write_on_test_file1(void);

/* Test truncate() */
int cmd_fs7(int argc, char **argv)
{
   int rc;

   create_test_file1();
   write_on_test_file1();
   rc = truncate("/tmp/test1", 157);
   DEVSHELL_CMD_ASSERT(rc == 0);

   // TODO: add checks here to verify that the truncation worked as expected

   rc = unlink("/tmp/test1");
   DEVSHELL_CMD_ASSERT(rc == 0);
   return 0;
}
