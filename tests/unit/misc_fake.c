/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>

u32 spur_irq_count;
u32 unhandled_irq_count[256];

bool suppress_printk;
volatile bool __in_panic;
volatile bool __in_kernel_shutdown;
volatile bool __in_panic_debugger;
void *__kernel_pdir;

void panic(const char *fmt, ...)
{
   printf("\n--- KERNEL PANIC ---\n");

   va_list args;
   va_start(args, fmt);
   vprintf(fmt, args);
   va_end(args);

   printf("\n--- END PANIC MESSAGE ---\n");
   abort();
}

void printk(const char *fmt, ...)
{
   if (suppress_printk)
      return;

   va_list args;
   va_start(args, fmt);
   vprintf(fmt, args);
   va_end(args);
}

void assert_failed(const char *expr, const char *file, int line)
{
   printf("Kernel assertion '%s' FAILED in %s:%d\n", expr, file, line);
   abort();
}

void not_reached(const char *file, int line)
{
   printf("Kernel NOT_REACHED statement in %s:%d\n", file, line);
   abort();
}

void not_implemented(const char *file, int line)
{
   printf("Kernel NOT_IMPLEMENTED at %s:%d\n", file, line);
   abort();
}

int fat_ramdisk_prepare_for_mmap(void *hdr, size_t rd_size)
{
   return -1;
}

int wth_create_thread_for(void *t) { return 0; }
void wth_wakeup() { /* do nothing */ }
void check_in_irq_handler() { /* do nothing */ }

void kmutex_lock(struct kmutex *m) {
   ASSERT(m->owner_task == NULL);
   m->owner_task = get_curr_task();
}

void kmutex_unlock(struct kmutex *m) {
   ASSERT(m->owner_task == get_curr_task());
   m->owner_task = NULL;
}
