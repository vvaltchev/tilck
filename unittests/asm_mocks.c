
#include <common/basic_defs.h>

// Defining some necessary symbols just to make the linker happy.

void *kernel_initial_stack = NULL;

void kernel_yield() { NOT_REACHED(); }
void arch_specific_new_task_setup() { NOT_REACHED(); }
void arch_specific_free_task() { NOT_REACHED(); }
void switch_to_initial_kernel_stack() { NOT_REACHED(); }
