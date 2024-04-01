/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/datetime.h>

void hw_read_clock(struct datetime *out)
{
   memset(out, 0, sizeof(*out));
}

bool hi_vmem_avail(void) { return false; }
int kthread_create2() { return -12; /* ENOMEM */}

void invalidate_page() {}
void init_serial_port() { }
void serial_write() { }
void handle_fault() { }
void handle_syscall() { }
void arch_irq_handling() { }
void pic_send_eoi() { }
void task_info_reset_kernel_stack() { }
void set_kernel_stack() { }
void irq_clear_mask() { }
void debug_qemu_turn_off_machine() { }
void setup_process() { }
void gdt_install() { }
void idt_install() { }
void irq_install() { }
void hw_timer_setup() { }
void irq_install_handler() { }
void irq_uninstall_handler() { }
void setup_sysenter_interface() { }
void save_current_task_state() { }
void switch_to_task() { }
void pdir_clone() { }
void pdir_deep_clone() { }
void pdir_destroy() { }
void set_curr_pdir() { }
void set_current_task_in_user_mode() { }
void arch_specific_new_task_setup() { NOT_REACHED(); }
void arch_specific_free_task() { NOT_REACHED(); }
void arch_specific_new_proc_setup() { NOT_REACHED(); }
void arch_specific_free_proc() { NOT_REACHED(); }
void fpu_context_begin() { }
void fpu_context_end() { }
void map_zero_pages() { NOT_REACHED(); }
void dump_var_mtrrs() { }
void set_page_rw() { }
void poweroff() { NOT_REACHED(); }
int get_irq_num(void *ctx) { return -1; }
int get_int_num(void *ctx) { return -1; }
void retain_pageframes_mapped_at() { }
void release_pageframes_mapped_at() { }
bool irq_is_masked() { NOT_REACHED(); return false; }

void *hi_vmem_reserve(size_t size) { return NULL; }
void hi_vmem_release(void *ptr, size_t size) { }
void on_first_pdir_update(void) { }

void *get_syscall_func_ptr(u32 n) { return NULL; }
int get_syscall_num(void *func) { return -1; }

void arch_add_initial_mem_regions() { }
bool arch_add_final_mem_regions() { return false; }
void setup_sig_handler() { NOT_REACHED(); }
void setup_pause_trampoline() { NOT_REACHED(); }
void debug_validate_resume_ip(void *state) { }
