
#pragma once

#if defined(__i386__)

#  include <arch/generic_x86/utils.h>
#  include <arch/i386/arch_utils.h>
#  include <arch/i386/process_int.h>

#elif defined(__x86_64__)

#  include <arch/generic_x86/utils.h>

// Hack for making the build of unit tests to pass.
#  if defined(KERNEL_TEST)
#     include <arch/i386/arch_utils.h>
#     include <arch/i386/process_int.h>
#  endif

#else

#error Unsupported architecture.

#endif


void setup_syscall_interface();
void set_kernel_stack(uptr stack);
uptr get_kernel_stack();

void disable_preemption_for(int jiffies);


#define RAM_DISK_PADDR (0x8000000U) // +128 M
#define RAM_DISK_VADDR (0xCA000000U)
#define RAM_DISK_SIZE (16 * MB)
