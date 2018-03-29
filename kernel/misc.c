
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/hal.h>

/* the kernel starts with interrupts disabled */
volatile int disable_interrupts_count = 1;

uptr ramdisk_paddr = RAMDISK_PADDR; /* default value in case of no multiboot */
size_t ramdisk_size = RAMDISK_SIZE; /* default value in case of no multiboot */

char symtab_buf[16*KB] __attribute__ ((section (".Symtab"))) = {0};
char strtab_buf[16*KB] __attribute__ ((section (".Strtab"))) = {0};

#ifdef DEBUG

void validate_stack_pointer_int(const char *file, int line)
{
   if (!current || current->pid == -1 /* fake current process */) {
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
