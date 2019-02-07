/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <stdbool.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/syscalls.h>
#include "sysenter.h"

/* configuration */
#define MAX_ARGS 16
#define FORK_TEST_ITERS (250 * MB)

/* utils */
#define RDTSC() __builtin_ia32_rdtsc()

#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[93m"
#define RESET_ATTRS   "\033[0m"
/* --- */

extern bool dump_coverage;

typedef int (*cmd_func_type)(int argc, char **argv);

void run_if_known_command(const char *cmd, int argc, char **argv);
void dump_list_of_commands(void);
int read_command(char *buf, int buf_size);
void dump_coverage_files(void);

static inline int tilck_get_num_gcov_files(void)
{
   return sysenter_call1(TILCK_TESTCMD_SYSCALL,
                         TILCK_TESTCMD_GCOV_GET_NUM_FILES);
}

static inline int
tilck_get_gcov_file_info(int fn,
                         char *fname,
                         unsigned fname_size,
                         unsigned *fsize)
{
   return sysenter_call5(TILCK_TESTCMD_SYSCALL,
                         TILCK_TESTCMD_GCOV_FILE_INFO,
                         fn, fname, fname_size, fsize);
}

static inline int
tilck_get_gcov_file(int fn, char *buf)
{
   return sysenter_call3(TILCK_TESTCMD_SYSCALL,
                         TILCK_TESTCMD_GCOV_GET_FILE,
                         fn, buf);
}

static inline int
tilck_debug_qemu_poweroff(void)
{
   return sysenter_call1(TILCK_TESTCMD_SYSCALL, TILCK_TESTCMD_QEMU_POWEROFF);
}
