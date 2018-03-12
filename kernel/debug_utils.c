
#include <common_defs.h>

#ifndef UNIT_TEST_ENVIRONMENT

#include <debug_utils.h>
#include <string_util.h>
#include <hal.h>
#include <irq.h>
#include <process.h>
#include <elf.h>
#include <multiboot.h>

void panic_save_current_state();
volatile bool in_panic = false;

static bool mapped_in_kernel_or_in_pdir(page_directory_t *pdir, void *vaddr)
{
   return is_mapped(pdir, vaddr) || is_mapped(get_kernel_page_dir(), vaddr);
}

size_t stackwalk32(void **frames,
                   size_t count,
                   void *ebp,
                   page_directory_t *pdir)
{
   void *retAddr;
   size_t i;

   if (!ebp) {
      ebp = (void *) (&frames - 2);
   }

   if (!pdir) {
      pdir = get_kernel_page_dir();
   }

   for (i = 0; i < count; i++) {

      void *addrs_to_deref[2] = { ebp, ebp + 1 };
      if (!mapped_in_kernel_or_in_pdir(pdir, addrs_to_deref[0]) ||
          !mapped_in_kernel_or_in_pdir(pdir, addrs_to_deref[1])) {

         break;
      }

      retAddr = *((void **)ebp + 1);
      ebp = *(void **)ebp;

      if (!ebp || !retAddr) {
         break;
      }

      frames[i] = retAddr;
   }

   return i;
}


void dump_stacktrace()
{
   void *frames[32] = {0};
   size_t c = stackwalk32(frames, ARRAY_SIZE(frames), NULL, NULL);

   printk("Stacktrace: ");

   /* i starts with 1, in order to skip the frame of this function call. */
   for (size_t i = 1; i < c; i++) {
      printk("%p ", frames[i]);
   }

   printk("\n");
}


#ifdef __i386__


uptr find_addr_of_symbol(const char *searched_sym)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)(KERNEL_PA_TO_VA(KERNEL_PADDR));
   VERIFY(h->e_shentsize == sizeof(Elf32_Shdr));

   Elf32_Shdr *sections = (Elf32_Shdr *) ((char *)h + h->e_shoff);
   Elf32_Shdr *symtab = NULL;
   Elf32_Shdr *strtab = NULL;

   for (u32 i = 0; i < h->e_shnum; i++) {
      Elf32_Shdr *s = sections + i;

      if (s->sh_type == SHT_SYMTAB) {
         ASSERT(!symtab);
         symtab = s;
      } else if (s->sh_type == SHT_STRTAB && i != h->e_shstrndx) {
         ASSERT(!strtab);
         strtab = s;
      }
   }

   VERIFY(symtab != NULL);
   VERIFY(strtab != NULL);

   Elf32_Sym *syms = (Elf32_Sym *) symtab->sh_addr;
   const int sym_count = symtab->sh_size / sizeof(Elf32_Sym);

   for (int i = 0; i < sym_count; i++) {
      if (!strcmp((char *)strtab->sh_addr + syms[i].st_name, searched_sym))
         return syms[i].st_value;
   }

   return 0;
}


void debug_qemu_turn_off_machine()
{
   outb(0xf4, 0x00);
}

void dump_regs(regs *r)
{
   printk("Registers: eflags: %p\n", r->eflags);

   printk("ss:  %p, cs:  %p, ds:  %p, esp: %p\n",
          r->ss, r->cs, r->ds, r->useresp);

   printk("eip: %p, eax: %p, ecx: %p, edx: %p\n",
          r->eip, r->eax, r->ecx, r->edx);

   printk("ebx: %p, ebp: %p, esi: %p, edi: %p\n",
          r->ebx, r->ebp, r->esi, r->edi);
}

void dump_raw_stack(uptr addr)
{
   printk("Raw stack dump:\n");

   for (int i = 0; i < 36; i += 4) {

      printk("%p: ", addr);

      for (int j = 0; j < 4; j++) {
         printk("%p ", *(void **)addr);
         addr += sizeof(uptr);
      }

      printk("\n");
   }
}

#endif


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

   if (get_current_task()) {
      printk("Current process: %i %s\n",
             get_current_task()->pid,
             is_kernel_thread(get_current_task()) ? "[KERNEL]" : "[USER]");
   } else {
      printk("Current process: NONE\n");
   }

   printk("Interrupts: [ ");
   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      printk("%i ", nested_interrupts[i]);
   }
   printk("]\n");

   dump_stacktrace();
   dump_regs(&current->kernel_state_regs);
   //dump_raw_stack((uptr) &fmt);


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
