/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>

#include <tilck_gen_headers/config_kmalloc.h>

#include <tilck/kernel/kmalloc.h>
#include <kernel/kmalloc/kmalloc_heap_struct.h> // kmalloc private header
#include <kernel/kmalloc/kmalloc_block_node.h>  // kmalloc private header

u32 spur_irq_count;
u32 unhandled_irq_count[256];

bool suppress_printk;
volatile bool __in_panic;
volatile bool __in_kernel_shutdown;
volatile bool __in_panic_debugger;
void *__kernel_pdir;
bool mock_kmalloc = false; /* see the comments above __wrap_general_kmalloc() */

void *__real_general_kmalloc(size_t *size, u32 flags);
void __real_general_kfree(void *ptr, size_t *size, u32 flags);

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

void __wrap_tilck_vprintk(u32 flags, const char *fmt, va_list args)
{
   if (suppress_printk)
      return;

   vprintf(fmt, args);
}

void __wrap_assert_failed(const char *expr, const char *file, int line)
{
   printf("Kernel assertion '%s' FAILED in %s:%d\n", expr, file, line);
   abort();
}

void __wrap_not_reached(const char *file, int line)
{
   printf("Kernel NOT_REACHED statement in %s:%d\n", file, line);
   abort();
}

void __wrap_not_implemented(const char *file, int line)
{
   printf("Kernel NOT_IMPLEMENTED at %s:%d\n", file, line);
   abort();
}

int __wrap_fat_ramdisk_prepare_for_mmap(void *hdr, size_t rd_size)
{
   return -1;
}

int __wrap_wth_create_thread_for(void *t) { return 0; }
void __wrap_wth_wakeup() { /* do nothing */ }
void __wrap_check_in_irq_handler() { /* do nothing */ }

void __wrap_kmutex_lock(struct kmutex *m) {
   ASSERT(m->owner_task == NULL);
   m->owner_task = get_curr_task();
}

void __wrap_kmutex_unlock(struct kmutex *m) {
   ASSERT(m->owner_task == get_curr_task());
   m->owner_task = NULL;
}

/*
 * Decide with just a global flag whether to use glibc's malloc() or Tilck's
 * kmalloc() implementation, instead of using a proper GMock in kmalloc_test.cpp
 * with ON_CALL(mock, general_kmalloc).WillByDefault([&mock](...) { ... }),
 * simply because that is too slow for performance measurements. Otherwise, the
 * mocking mechanism with GMock is great.
 */

void *__wrap_general_kmalloc(size_t *size, u32 flags)
{
   if (mock_kmalloc)
      return malloc(*size);

   return __real_general_kmalloc(size, 0);
}

void __wrap_general_kfree(void *ptr, size_t *size, u32 flags)
{
   if (mock_kmalloc)
      return free(ptr);

   return __real_general_kfree(ptr, size, 0);
}

void *__wrap_kmalloc_get_first_heap(size_t *size)
{
   static void *buf;

   if (!buf) {
      buf = aligned_alloc(KMALLOC_MAX_ALIGN, KMALLOC_FIRST_HEAP_SIZE);
      VERIFY(buf);
      VERIFY( ((ulong)buf & (KMALLOC_MAX_ALIGN - 1)) == 0 );
   }

   if (size)
      *size = KMALLOC_FIRST_HEAP_SIZE;

   return buf;
}
