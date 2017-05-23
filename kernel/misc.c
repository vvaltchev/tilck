
#include <common_defs.h>
#include <string_util.h>
#include <hal.h>

/* the kernel starts with interrupts disabled */
volatile int disable_interrupts_count = 1;

void validate_stack_pointer()
{
   if (!current) {
      return;
   }

   uptr stack_var = 123;

   if (((uptr)&stack_var & PAGE_MASK) != (uptr)current->kernel_stack) {

      disable_interrupts_forced();

      printk("\n[validate stack] real stack page:       %p\n",
             ((uptr)&stack_var & PAGE_MASK));
      printk("[validate stack] current->kernel_stack: %p\n",
             current->kernel_stack);

      NOT_REACHED();
   }
}
