/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/syscalls.h>

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
   syscall(TILCK_CMD_SYSCALL, TILCK_CMD_DEBUG_PANEL);
   return 0;
}
