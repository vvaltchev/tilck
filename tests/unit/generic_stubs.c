/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/datetime.h>

struct hw_timer_info;
struct irq_handler_node;
struct process;
struct task;

void hw_read_clock(struct datetime *out)
{
   memset(out, 0, sizeof(*out));
}

bool hi_vmem_avail(void) { return false; }
void setup_usermode_task_regs(void *r, void *entry, void *stack_addr) { }
void invalidate_page(ulong vaddr) { }
void init_serial_port(u16 port) { }
void serial_write(u16 port, char c) { }
void handle_fault(void *r) { }
void handle_syscall(void *r) { }
void arch_irq_handling(void *r) { }
void pic_send_eoi(int irq) { }
void set_kernel_stack(ulong stack) { }
void irq_clear_mask(int irq) { }
void debug_qemu_turn_off_machine(void) { }
void gdt_install(void) { }
void idt_install(void) { }
void irq_install(void) { }
void hw_timer_setup(u32 hz, struct hw_timer_info *out) { }
void irq_install_handler(u8 irq, struct irq_handler_node *n) { }
void irq_uninstall_handler(u8 irq, struct irq_handler_node *n) { }
void setup_sysenter_interface(void) { }
void pdir_clone(void *pdir) { }
void pdir_deep_clone(void *pdir) { }
void pdir_destroy(void *pdir) { }
void set_curr_pdir(void *pdir) { }
void arch_specific_new_proc_setup(struct process *pi, struct process *parent) { NOT_REACHED(); }
void arch_specific_free_proc(struct process *pi) { NOT_REACHED(); }
void fpu_context_begin(void) { }
void fpu_context_end(void) { }
void map_zero_pages(void *pdir, void *vaddrp, size_t page_count, u32 pg_flags) { NOT_REACHED(); }
void dump_var_mtrrs(void) { }
void set_page_rw(void *pdir, void *vaddr, bool rw) { }
void poweroff(void) { NOT_REACHED(); }
int get_irq_num(void *ctx) { return -1; }
int get_int_num(void *ctx) { return -1; }
void retain_pageframes_mapped_at(void *pdir, void *vaddr, size_t len) { }
void release_pageframes_mapped_at(void *pdir, void *vaddr, size_t len) { }
bool irq_is_masked(int irq) { NOT_REACHED(); return false; }
void dump_stacktrace(void *ebp, void *pdir) { NOT_REACHED(); }
bool allocate_fpu_regs(void *arch_fields) { NOT_REACHED(); return false; }
/*
 * Reachable from init_sched() -> kthread_create(&idle, ...) when the
 * scheduler gtest exercises the real init_sched(). The actual register
 * state set up here is never executed in the test (switch_to_task /
 * context_switch are unreachable on aarch64 host), so a no-op is fine.
 */
void kthread_create_init_regs_arch(void *r, void *func) { }
void kthread_create_setup_initial_stack(struct task *ti, void *r, void *arg) { }
void save_curr_fpu_ctx_if_enabled(void) { }
void arch_usermode_task_switch(struct task *ti) { }

/*
 * hi_vmem_reserve / hi_vmem_release: real test fakes in
 * tests/unit/mm_fakes.cpp -- they need to return malloc'd memory
 * for code paths like alloc_kernel_isolated_stack() to round-trip
 * correctly through the page-table fakes.
 */
void on_first_pdir_update(void) { }

void *get_syscall_func_ptr(u32 n) { return NULL; }
int get_syscall_num(void *func) { return -1; }

void arch_add_initial_mem_regions(void) { }
bool arch_add_final_mem_regions(void) { return false; }
void setup_sig_handler(struct task *ti, void *r, ulong user_func, int signum) { NOT_REACHED(); }
