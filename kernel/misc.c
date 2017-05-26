
#include <common_defs.h>
#include <string_util.h>
#include <hal.h>

/* the kernel starts with interrupts disabled */
volatile int disable_interrupts_count = 1;

#ifdef DEBUG

void validate_stack_pointer_int(const char *file, int line)
{
   if (!current) {
      return;
   }

   uptr stack_var = 123;

   if (((uptr)&stack_var & PAGE_MASK) != (uptr)current->kernel_stack) {

      disable_interrupts_forced();

      panic("\n"
            "[validate stack] real stack page:       %p\n"
            "[validate stack] current->kernel_stack: %p\n"
            "Invalid kernel stack pointer.\n"
            "File %s at line %i\n",
            ((uptr)&stack_var & PAGE_MASK),
            current->kernel_stack, file, line);
   }
}

#endif
