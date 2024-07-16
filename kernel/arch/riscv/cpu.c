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

void enable_cpu_features(void)
{
   NOT_IMPLEMENTED();
}

void save_current_fpu_regs(bool in_kernel)
{
   NOT_IMPLEMENTED();
}

void restore_fpu_regs(void *task, bool in_kernel)
{
   NOT_IMPLEMENTED();
}

void restore_current_fpu_regs(bool in_kernel)
{
   NOT_IMPLEMENTED();
}

bool allocate_fpu_regs(arch_task_members_t *arch_fields)
{
   NOT_IMPLEMENTED();
}

void fpu_context_begin(void)
{
   NOT_IMPLEMENTED();
}

void fpu_context_end(void)
{
   NOT_IMPLEMENTED();
}

void init_fpu_memcpy(void)
{
   /* STUB function: do nothing */
}
