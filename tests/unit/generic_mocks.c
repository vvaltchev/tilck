/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <tilck/common/basic_defs.h>

u32 spur_irq_count;
u32 unhandled_irq_count[256];

bool suppress_printk;

void panic(const char *fmt, ...)
{
   printf("\n--- KERNEL PANIC ---\n");

   va_list args;
   va_start(args, fmt);
   vprintf(fmt, args);
   va_end(args);

   printf("\n--- END PANIC MESSAGE ---\n");
   abort();
}

void __wrap_printk(const char *fmt, ...)
{
   if (suppress_printk)
      return;

	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

void __wrap_assert_failed(const char *expr, const char *file, int line)
{
	printf("Kernel assertion '%s' FAILED in %s:%d\n", expr, file, line);
   abort();
}

void __wrap_not_reached(const char *file, int line)
{
   printf("Kernel NOT_REACHED statement in %s:%d\n", file, line);
   abort();
}

void __wrap_not_implemented(const char *file, int line)
{
   printf("Kernel NOT_IMPLEMENTED at %s:%d\n", file, line);
   abort();
}

void __wrap_kmutex_lock(void *m) { }
void __wrap_kmutex_unlock(void *m) { }

void init_serial_port() { }
void serial_write() { }
void handle_fault() { }
void handle_syscall() { }
void handle_irq() { }
void pic_send_eoi() { }
void task_info_reset_kernel_stack() { }
void set_kernel_stack() { }
void irq_clear_mask() { }
void kthread_create() { }
void debug_qemu_turn_off_machine() { }
void setup_usermode_task() { }
void gdt_install() { }
void idt_install() { }
void irq_install() { }
void timer_set_freq() { }
void irq_install_handler() { }
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
void fpu_context_begin() { }
void fpu_context_end() { }
void cmos_read_datetime() { NOT_REACHED(); }
void map_zero_pages() { NOT_REACHED(); }
void dump_var_mtrrs() { }
void set_page_rw() { }
void kb_register_keypress_handler() { }
int get_irq_num(void *ctx) { return -1; }
int get_int_num(void *ctx) { return -1; }

bool kb_is_pressed() { return false; }
u32 kb_get_current_modifiers(void) { return 0; }
bool kb_scancode_to_ansi_seq() { return false; }
int kb_get_fn_key_pressed() { return 0; }
