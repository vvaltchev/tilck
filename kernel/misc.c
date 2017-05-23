
#include <common_defs.h>
#include <hal.h>

/* the kernel starts with interrupts disabled */
volatile int disable_interrupts_count = 1;

void validate_stack_pointer()
{
   uptr stack_var = 123;
   ASSERT(((uptr)&stack_var & PAGE_MASK) == (uptr)current->kernel_stack);
}
