/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/assert.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>

void handle_inst_illegal_fault_int(regs_t *r, const char *fault_name);
void asm_save_fpu(void *buf);
void asm_restore_fpu(void *buf);

struct riscv_fpu_regs {
   u64 f[32];
   u32 fcsr;
};

static void handle_inst_illegal_fpu_fault(regs_t *r)
{
   struct task *curr = get_curr_task();
   const int int_num = r->int_num;

   if (is_kernel_thread(curr) || in_syscall(curr))
      goto normal_illegal_fault;

   if (r->sstatus & SR_FS)
      goto normal_illegal_fault;

   if (!riscv_cpu_features.isa_exts.f &&
       !riscv_cpu_features.isa_exts.d &&
       !riscv_cpu_features.isa_exts.q)
      goto normal_illegal_fault;

   arch_task_members_t *arch_fields = get_task_arch_fields(curr);

#if FORK_NO_COW

   ASSERT(arch_fields->fpu_regs != NULL);

   /*
    * With the current implementation, even when the fpu_regs are
    * pre-allocated tasks cannot by default use the FPU. This approach has PROs
    * and CONs.
    *
    *    PROs:
    *       - the kernel doesn't have to save/restore unnecessarily the FPU ctx
    *       - tasks using the FPU can be distinguished from the others
    *       - it is simpler to make fpu_regs pre-allocated work this way
    *
    *    CONs:
    *       - paying the overhead of a not-strictly necessary fault, once.
    */

#else

    /*
     * We can hit this fault at MOST once in the lifetime of a task. These
     * sanity checks ensures that, in no case, we allocated the fpu_regs
     * and then, for any reason, we scheduled the task with FPU disabled.
     */

   if (arch_fields->fpu_regs)
      goto normal_illegal_fault;

   if (!allocate_fpu_regs(arch_fields)) {
      panic("Cannot allocate memory for the FPU context");
   }

#endif

   r->sstatus |= SR_FS;
   return;

normal_illegal_fault:
   handle_inst_illegal_fault_int(r, riscv_exception_names[int_num]);
}

void enable_cpu_features(void)
{
   get_cpu_features();
   hw_fpu_disable();
   set_fault_handler(EXC_INST_ILLEGAL, handle_inst_illegal_fpu_fault);
}

static struct riscv_fpu_regs fpu_kernel_regs;

void save_current_fpu_regs(bool in_kernel)
{
   if (UNLIKELY(in_panic()))
      return;

   arch_task_members_t *arch_fields = get_task_arch_fields(get_curr_task());
   void *buf = in_kernel ? &fpu_kernel_regs : arch_fields->fpu_regs;

   ASSERT(buf != NULL);
   asm_save_fpu(buf);
}

void restore_fpu_regs(void *task, bool in_kernel)
{
   if (UNLIKELY(in_panic()))
      return;

   arch_task_members_t *arch_fields = get_task_arch_fields((struct task *)task);
   void *buf = in_kernel ? &fpu_kernel_regs : arch_fields->fpu_regs;

   ASSERT(buf != NULL);
   asm_restore_fpu(buf);
}

void restore_current_fpu_regs(bool in_kernel)
{
   restore_fpu_regs(get_curr_task(), in_kernel);
}

bool allocate_fpu_regs(arch_task_members_t *arch_fields)
{
   ASSERT(arch_fields->fpu_regs == NULL);

   arch_fields->fpu_regs = kzalloc_obj(struct riscv_fpu_regs);
   arch_fields->fpu_regs_size = sizeof(struct riscv_fpu_regs);

   if (!arch_fields->fpu_regs)
      return false;

   return true;
}

static volatile bool in_fpu_context;

void fpu_context_begin(void)
{
   disable_preemption();

   /* NOTE: nested FPU contexts are NOT allowed (unless we're in panic) */

   if (LIKELY(!in_panic())) {
      ASSERT(!in_fpu_context);
   }

   in_fpu_context = true;
   hw_fpu_enable();
   save_current_fpu_regs(true);
}

void fpu_context_end(void)
{
   ASSERT(in_fpu_context);

   restore_current_fpu_regs(true);
   hw_fpu_disable();

   in_fpu_context = false;
   enable_preemption();
}

void init_fpu_memcpy(void)
{
   /* STUB function: do nothing */
}

