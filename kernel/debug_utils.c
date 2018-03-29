
#include <common/basic_defs.h>
#include <common/string_util.h>

volatile bool in_panic;

#ifndef UNIT_TEST_ENVIRONMENT

#include <exos/debug_utils.h>
#include <exos/hal.h>
#include <exos/irq.h>
#include <exos/process.h>

#include <elf.h>
#include <multiboot.h>

void panic_save_current_state();

NORETURN void panic(const char *fmt, ...)
{
   disable_interrupts_forced();

   if (in_panic) {
      goto end;
   }

   in_panic = true;
   panic_save_current_state();

   printk("\n************************ KERNEL PANIC ************************\n");

   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);

   printk("\n");

   task_info *curr = get_current_task();

   if (curr && curr->pid != -1) {
      printk("Current process: %i %s\n",
             get_current_task()->pid,
             is_kernel_thread(get_current_task()) ? "[KERNEL]" : "[USER]");
   } else {
      printk("Current process: NONE\n");
   }

   if (nested_interrupts_count > 0) {
      printk("Interrupts: [ ");
      for (int i = nested_interrupts_count - 1; i >= 0; i--) {
         printk("%i ", nested_interrupts[i]);
      }
      printk("]\n");
   }

   //dump_raw_stack((uptr) &fmt);
   dump_regs(current->kernel_state_regs);
   dump_stacktrace();


#ifdef DEBUG_QEMU_EXIT_ON_PANIC
   debug_qemu_turn_off_machine();
#endif

end:

   while (true) {
      halt();
   }
}

void dump_multiboot_info(multiboot_info_t *mbi)
{
   printk("MBI ptr: %p [+ %u KB]\n", mbi, ((uptr)mbi)/KB);
   printk("mem lower: %u KB\n", mbi->mem_lower + 1);
   printk("mem upper: %u MB\n", (mbi->mem_upper)/1024 + 1);

   if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
      printk("Cmdline: '%s'\n", (char *)(uptr)mbi->cmdline);
   }

   if (mbi->flags & MULTIBOOT_INFO_VBE_INFO) {
      printk("VBE mode: %p\n", mbi->vbe_mode);
   }

   if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
      printk("Framebuffer addr: %p\n", mbi->framebuffer_addr);
   }

   if (mbi->flags & MULTIBOOT_INFO_MODS) {

      printk("Mods count: %u\n", mbi->mods_count);

      for (u32 i = 0; i < mbi->mods_count; i++) {

         multiboot_module_t *mod =
            ((multiboot_module_t *)(uptr)mbi->mods_addr)+i;

         printk("mod cmdline: '%s'\n", mod->cmdline);
         printk("mod start: %p [+ %u KB]\n", mod->mod_start,
                                             mod->mod_start/KB);
         printk("mod end:   %p [+ %u KB]\n", mod->mod_end,
                                             mod->mod_end/KB);
         printk("mod size:  %u KB\n", (mod->mod_end-mod->mod_start)/KB);
      }

   }

   if (mbi->flags & MULTIBOOT_INFO_ELF_SHDR) {
      printk("ELF section table available\n");
      printk("num:   %u\n", mbi->u.elf_sec.num);
      printk("addr:  %p\n", mbi->u.elf_sec.addr);
      printk("size:  %u\n", mbi->u.elf_sec.size);
      printk("shndx: %p\n", mbi->u.elf_sec.shndx);
   }
}

#endif // UNIT_TEST_ENVIRONMENT
