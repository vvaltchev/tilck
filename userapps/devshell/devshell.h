/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <stdbool.h>
#include <unistd.h>
#include <sys/syscall.h>

#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>    /* for __rdtsc() */
#elif defined(__riscv)
   #include <tilck/common/arch/riscv/riscv_utils.h>
   #undef RDTSC
#endif

#include <tilck/common/basic_defs.h>
#include <tilck/common/syscalls.h>
#include "sysenter.h"

typedef unsigned long long ull_t;

/* configuration */
#define MAX_ARGS                  16
#define FORK_TEST_ITERS   (250 * MB)

/* constants */
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[93m"
#define RESET_ATTRS   "\033[0m"
#define PFX           "[devshell] "
#define STR_PARENT    "[   parent   ] "
#define STR_CHILD     "[child] "
#define STR_RUN       "[RUN   ] "
#define STR_PASS      "[PASSED] "
#define STR_FAIL      "[FAILED] "

/* utils */
#define RDTSC()                     __rdtsc()
#define DEBUG_BREAK()               asmVolatile("int $3")
#define DEVSHELL_CMD_ASSERT(x)                                              \
   do {                                                                     \
      if (!(x)) {                                                           \
         fprintf(stderr, PFX "ASSERT '%s' FAILED.\n\t"                      \
                 "On:\t%s:%d\n\tERRNO:\t%s\n",                              \
                 #x, __FILE__, __LINE__, strerror(errno));                  \
         do_cmd_exit(1);                                                    \
      }                                                                     \
   } while(0)

/* --- */

extern bool dump_coverage;
extern char **shell_argv;
extern char **shell_env;

typedef int (*cmd_func_type)(int argc, char **argv);

enum timeout_type {
   TT_SHORT = 0,
   TT_MED   = 1,
   TT_LONG  = 2,
};

struct test_cmd_entry {
   const char *name;
   cmd_func_type func;
   enum timeout_type tt;
   bool enabled_in_st;
};

void run_if_known_command(const char *cmd, int argc, char **argv);
void dump_list_of_commands_and_exit(void);
int read_command(char *buf, int buf_size);
void dump_coverage_files(void);
void show_common_help_intro(void);
const char *get_devshell_path(void);
void print_waitpid_change(int child, int wstatus);
void forced_memcpy(void *dest, const void *src, size_t n);
void do_mm_read(void *ptr);
void do_cmd_exit(int code);
bool is_stack_aligned_16(void);
void execute_illegal_instruction(void);
size_t mm_estimate_usable_mem(void);

/* From the man page of getdents64() */
struct linux_dirent64 {
   u64            d_ino;    /* 64-bit inode number */
   u64            d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};

static inline int
getdents64(unsigned fd, struct linux_dirent64 *dirp, unsigned count)
{
#ifdef __i386__
   ASSERT(SYS_getdents64 == 220);
#endif

   return sysenter_call3(SYS_getdents64, fd, dirp, count);
}

static inline int tilck_get_num_gcov_files(void)
{
   return sysenter_call1(TILCK_CMD_SYSCALL,
                         TILCK_CMD_GCOV_GET_NUM_FILES);
}

static inline int
tilck_get_gcov_file_info(int fn,
                         char *fname,
                         unsigned fname_size,
                         unsigned *fsize)
{
   return sysenter_call5(TILCK_CMD_SYSCALL,
                         TILCK_CMD_GCOV_FILE_INFO,
                         fn, fname, fname_size, fsize);
}

static inline int
tilck_get_gcov_file(int fn, char *buf)
{
   return sysenter_call3(TILCK_CMD_SYSCALL,
                         TILCK_CMD_GCOV_GET_FILE,
                         fn, buf);
}

static inline int
tilck_debug_qemu_poweroff(void)
{
   return sysenter_call1(TILCK_CMD_SYSCALL, TILCK_CMD_QEMU_POWEROFF);
}

static inline int
tilck_set_sched_alive_thread_enabled(bool enabled)
{
   return sysenter_call2(TILCK_CMD_SYSCALL,
                         TILCK_CMD_SET_SAT_ENABLED,
                         enabled);
}
