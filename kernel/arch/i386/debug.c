
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/debug_utils.h>
#include <exos/hal.h>
#include <exos/irq.h>
#include <exos/process.h>

#include <elf.h>
#include <multiboot.h>

static bool mapped_in_pdir(page_directory_t *pdir, void *vaddr)
{
   if (!get_kernel_page_dir()) {

      // Paging has not been initialized yet.
      // Just check if vaddr is in the first 4 MB (and in BASE_VA + 4 MB).

      uptr va = (uptr)vaddr;
      return va < (4*MB)||(KERNEL_BASE_VA <= va && va < (KERNEL_BASE_VA+4*MB));
   }

   return is_mapped(pdir, vaddr);
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

      if (!mapped_in_pdir(pdir, addrs_to_deref[0]))
         break;

      if (!mapped_in_pdir(pdir, addrs_to_deref[1]))
         break;

      retAddr = *((void **)ebp + 1);
      ebp = *(void **)ebp;

      if (!ebp || !retAddr) {
         break;
      }

      frames[i] = retAddr;
   }

   return i;
}


void dump_stacktrace(void)
{
   void *frames[32] = {0};
   size_t c = stackwalk32(frames, ARRAY_SIZE(frames), NULL, NULL);

   printk("Stacktrace (%u frames):\n", c);

   for (size_t i = 0; i < c; i++) {
      ptrdiff_t off;
      uptr va = (uptr)frames[i];
      const char *sym_name = find_sym_at_addr(va, &off);
      printk("[%p] %s + 0x%x\n", va, sym_name ? sym_name : "???", off);
   }

   printk("\n");
}

void debug_qemu_turn_off_machine()
{
   outb(0xf4, 0x00);
}

void dump_eflags(u32 f)
{
   printk("eflags: %p [ %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s ], IOPL: %u\n",
          f,
          f & EFLAGS_CF ? "CF " : "",
          f & EFLAGS_PF ? "PF " : "",
          f & EFLAGS_AF ? "AF " : "",
          f & EFLAGS_ZF ? "ZF " : "",
          f & EFLAGS_SF ? "SF " : "",
          f & EFLAGS_TF ? "TF " : "",
          f & EFLAGS_IF ? "IF " : "",
          f & EFLAGS_DF ? "DF " : "",
          f & EFLAGS_OF ? "OF " : "",
          f & EFLAGS_NT ? "NT " : "",
          f & EFLAGS_RF ? "RF " : "",
          f & EFLAGS_VM ? "VM " : "",
          f & EFLAGS_AC ? "AC " : "",
          f & EFLAGS_VIF ? "VIF " : "",
          f & EFLAGS_VIP ? "VIP " : "",
          f & EFLAGS_ID ? "ID " : "",
          f & EFLAGS_IOPL);
}

void dump_regs(regs *r)
{
   dump_eflags(r->eflags);

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
