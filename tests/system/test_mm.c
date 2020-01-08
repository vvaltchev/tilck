/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include "devshell.h"
#include "sysenter.h"

int cmd_brk(int argc, char **argv)
{
   const size_t alloc_size = 1024 * 1024;

   void *orig_brk = (void *)syscall(SYS_brk, 0);
   void *b = orig_brk;

   size_t tot_allocated = 0;

   for (int i = 0; i < 128; i++) {

      void *new_brk = b + alloc_size;

      b = (void *)syscall(SYS_brk, b + alloc_size);

      if (b != new_brk)
         break;

      tot_allocated += alloc_size;
   }

   //printf("tot allocated: %u KB\n", tot_allocated / 1024);

   b = (void *)syscall(SYS_brk, orig_brk);

   if (b != orig_brk) {
      printf("Unable to free mem with brk()\n");
      return 1;
   }

   return 0;
}

int cmd_mmap(int argc, char **argv)
{
   const int iters_count = 10;
   const size_t alloc_size = 1 * MB;
   void *arr[1024];
   int max_mb = -1;

   ull_t tot_duration = 0;

   for (int iter = 0; iter < iters_count; iter++) {

      int i;
      ull_t start = RDTSC();

      for (i = 0; i < 64; i++) {

         errno = 0;

         void *res = mmap(NULL,
                          alloc_size,
                          PROT_READ | PROT_WRITE,
                          MAP_ANONYMOUS | MAP_PRIVATE,
                          -1,
                          0);

         if (res == (void*) -1) {
            i--;
            break;
         }

         arr[i] = res;
      }

      i--;
      tot_duration += (RDTSC() - start);

      if (max_mb < 0) {

         max_mb = i;

      } else {

         if (i != max_mb) {
            printf("[iter: %u] Unable to alloc max_mb (%u) as previous iters\n",
                   iter, max_mb);
            return 1;
         }
      }

      printf("[iter: %u][mmap_test] Mapped %u MB\n", iter, i + 1);

      start = RDTSC();

      for (; i >= 0; i--) {

         int rc = munmap(arr[i], alloc_size);

         if (rc != 0) {
            printf("munmap(%p) failed with error: %s\n",
                   arr[i], strerror(errno));
            return 1;
         }
      }

      tot_duration += (RDTSC() - start);
   }

   printf("\nAvg. cycles for mmap + munmap %u MB: %llu million\n",
          max_mb + 1, (tot_duration / iters_count) / 1000000);

   return 0;
}

static void no_munmap_bad_child(void)
{
   const size_t alloc_size = 128 * KB;

   void *res = mmap(NULL,
                    alloc_size,
                    PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE,
                    -1,
                    0);

   if (res == (void*) -1) {
      printf("[child] mmap 128 KB failed!\n");
      exit(1);
   }

   /* DO NOT munmap the memory, expecting the kernel to do that! */
   exit(0);
}

int cmd_mmap2(int argc, char **argv)
{
   int child;
   int wstatus;

   child = fork();

   if (!child)
      no_munmap_bad_child();

   waitpid(child, &wstatus, 0);
   return 0;
}
