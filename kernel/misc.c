
#include <common_defs.h>
#include <string_util.h>
#include <process.h>
#include <hal.h>

/* the kernel starts with interrupts disabled */
volatile int disable_interrupts_count = 1;

task_info *usermode_init_task;
uptr ramdisk_paddr = RAMDISK_PADDR; /* default value in case of no multiboot */
size_t ramdisk_size = RAMDISK_SIZE; /* default value in case of no multiboot */

#ifdef DEBUG

void validate_stack_pointer_int(const char *file, int line)
{
   if (!current) {
      return;
   }

   uptr stack_var = 123;

   if (((uptr)&stack_var & PAGE_MASK) != (uptr)current->kernel_stack) {

      disable_interrupts_forced();

      panic("Invalid kernel stack pointer.\n"
            "File %s at line %i\n"
            "[validate stack] real stack page:       %p\n"
            "[validate stack] current->kernel_stack: %p\n",
            file, line,
            ((uptr)&stack_var & PAGE_MASK),
            current->kernel_stack);
   }
}

#endif
