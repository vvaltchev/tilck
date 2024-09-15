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

#include <elf.h>

static bool fp_is_valid(ulong fp, ulong sp)
{
   ulong low = sp + 2 * sizeof(void *);
   ulong high = (sp + (KERNEL_STACK_SIZE - 1)) & ~(KERNEL_STACK_SIZE - 1);

   if (!fp || fp < low || fp > high || fp & 0x7)
      return false;
   else
      return true;
}

static size_t
stackwalk_riscv(void **frames,
                size_t count,
                void *fp,
                pdir_t *pdir)
{
   bool curr_pdir = false;
   void *retAddr;
   size_t i;
   ulong old_fp;

   if (!fp) {
      fp = __builtin_frame_address(0);
   }

   if (!pdir) {
      pdir = get_curr_pdir();
      curr_pdir = true;
   }

   for (i = 0; i < count; i++) {

      old_fp = (ulong)fp;

      if ((ulong)fp < BASE_VA)
         break;

      if (curr_pdir) {

         retAddr = *((void **)fp - 1);
         fp = *((void **)fp - 2);

      } else {

         if (virtual_read(pdir, (void **)fp - 1,
                          &retAddr, sizeof(retAddr)) < 0)
            break;

         if (virtual_read(pdir, (void **)fp - 2,
                          &fp, sizeof(fp)) < 0)
            break;
      }

      if (!fp_is_valid((ulong)fp, old_fp)) {

         /*
          * In cases where the compiler lacks the option
          * -fno-omit-leaf-frame-pointer, in the leaf function,
          * fp is saved in the position where ra is in non-leaf case.
          */
         if (fp_is_valid((ulong)retAddr, old_fp)) {

            fp = retAddr;
            continue;
         }

         break;
      }

      frames[i] = retAddr;
   }

   return i;
}

void dump_stacktrace(void *ebp, pdir_t *pdir)
{
   void *frames[32] = {0};
   size_t c = stackwalk_riscv(frames, ARRAY_SIZE(frames), ebp, pdir);
   printk("Stacktrace (%lu frames):\n", c);

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

      printk("[%p] %s + 0x%lx\n", TO_PTR(va), sym_name ? sym_name : "???", off);
   }

   printk("\n");
}

void dump_sstatus(ulong sstatus)
{
   printk("sstatus: %p [ %s%s%s%s%s]\n",
          TO_PTR(sstatus),
          sstatus & SR_SIE        ? "SIE " : "",
          sstatus & SR_SPIE       ? "SPIE " : "",
          sstatus & SR_SPP        ? "SPP " : "",
          sstatus & SR_SUM        ? "SUM " : "",
          sstatus & SR_FS         ? "FS " : "");
}

void dump_regs(regs_t *r)
{
   dump_sstatus(r->sstatus);

   printk("ra: %p, sp: %p, gp: %p, tp: %p\n",
          TO_PTR(r->ra), TO_PTR(r->sp), TO_PTR(r->gp), TO_PTR(r->tp));

   printk("t0: %p, t1: %p, t2: %p, s0: %p\n",
          TO_PTR(r->t0), TO_PTR(r->t1), TO_PTR(r->t2), TO_PTR(r->s0));

   printk("s1: %p, a0: %p, a1: %p, a2: %p\n",
          TO_PTR(r->s1), TO_PTR(r->a0), TO_PTR(r->a1), TO_PTR(r->a2));

   printk("a3: %p, a4: %p, a5: %p, a6: %p\n",
          TO_PTR(r->a3), TO_PTR(r->a4), TO_PTR(r->a5), TO_PTR(r->a6));

   printk("a7: %p, s2: %p, s3: %p, s4: %p\n",
          TO_PTR(r->a7), TO_PTR(r->s2), TO_PTR(r->s3), TO_PTR(r->s4));

   printk("s5: %p, s6: %p, s7: %p, s8: %p\n",
          TO_PTR(r->s5), TO_PTR(r->s6), TO_PTR(r->s7), TO_PTR(r->s8));

   printk("s9: %p, s10: %p, s11: %p, t3: %p\n",
          TO_PTR(r->s9), TO_PTR(r->s10), TO_PTR(r->s11), TO_PTR(r->t3));

   printk("t4: %p, t5: %p, t6: %p, sepc: %p\n",
          TO_PTR(r->t4), TO_PTR(r->t5), TO_PTR(r->t6), TO_PTR(r->sepc));

   printk("sstatus: %p, sbadaddr: %p, scause: %p\n",
          TO_PTR(r->sstatus), TO_PTR(r->sbadaddr), TO_PTR(r->scause));

   printk("usersp: %p\n", TO_PTR(r->usersp));
}

int debug_qemu_turn_off_machine(void)
{
   poweroff();
   return 0;
}

void dump_raw_stack(ulong addr)
{
   printk("Raw stack dump:\n");

   for (int i = 0; i < 36; i += 4) {

      printk("%p: ", TO_PTR(addr));

      for (int j = 0; j < 4; j++) {
         printk("%p ", *(void **)addr);
         addr += sizeof(ulong);
      }

      printk("\n");
   }
}

