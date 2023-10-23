/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/arch/generic_x86/debug_utils_x86.h>

#include <elf.h>
#include <multiboot.h>

static size_t
stackwalk32(void **frames,
            size_t count,
            void *ebp,
            pdir_t *pdir)
{
   bool curr_pdir = false;
   void *retAddr;
   size_t i;

   if (!ebp) {
      ebp = __builtin_frame_address(0);
   }

   if (!pdir) {
      pdir = get_curr_pdir();
      curr_pdir = true;
   }

   for (i = 0; i < count; i++) {

      if ((ulong)ebp < BASE_VA)
         break;

      if (curr_pdir) {

         retAddr = *((void **)ebp + 1);
         ebp = *((void **)ebp);

      } else {

         if (virtual_read(pdir, (void **)ebp + 1, &retAddr, 4) < 0)
            break;

         if (virtual_read(pdir, (void **)ebp, &ebp, 4) < 0)
            break;
      }

      if (!ebp || !retAddr)
         break;

      frames[i] = retAddr;
   }

   return i;
}

void dump_stacktrace(void *ebp, pdir_t *pdir)
{
   void *frames[32] = {0};
   size_t c = stackwalk32(frames, ARRAY_SIZE(frames), ebp, pdir);

   printk("Stacktrace (%u frames):\n", c);

   for (size_t i = 0; i < c; i++) {

      long off = 0;
      u32 sym_size;
      ulong va = (ulong)frames[i];
      const char *sym_name;

      sym_name = find_sym_at_addr(va, &off, &sym_size);

      if (sym_name && off == 0) {

         /*
          * Since we're resolving return addresses, not addresses, we have to
          * keep in mind that offset == 0 means that the next instruction after
          * a call was the beginning of a new function. This happens when a
          * function calls a NORETURN function like panic(). In this case, in
          * order to correctly resolve the caller's function name, we need to
          * decrease the vaddr when searching for the symbol name.
          */

         sym_name = find_sym_at_addr(va - 1, &off, &sym_size);

         /*
          * Now we have to increase the offset value because in the backtrace
          * the original vaddr will be shown. [We passed "va-1" instead of "va"
          * because we wanted the previous function, now we have to adjust the
          * offset.]
          */

         off++;
      }

      printk("[%p] %s + %ld\n", TO_PTR(va), sym_name ? sym_name : "???", off);
   }

   printk("\n");
}

void dump_regs(regs_t *r)
{
   dump_eflags(r->eflags);

   printk("ss:  %p, cs:  %p, ds:  %p, esp: %p\n",
          TO_PTR(r->ss), TO_PTR(r->cs), TO_PTR(r->ds), TO_PTR(r->useresp));

   printk("eip: %p, eax: %p, ecx: %p, edx: %p\n",
          TO_PTR(r->eip), TO_PTR(r->eax), TO_PTR(r->ecx), TO_PTR(r->edx));

   printk("ebx: %p, ebp: %p, esi: %p, edi: %p\n",
          TO_PTR(r->ebx), TO_PTR(r->ebp), TO_PTR(r->esi), TO_PTR(r->edi));
}
