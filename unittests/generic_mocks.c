
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

bool suppress_printk;

void __wrap_printk(const char *fmt, ...)
{
   if (suppress_printk)
      return;

	printf("[kernel] ");

	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

void __wrap_panic(const char *fmt, ...)
{
	printf("--- KERNEL_PANIC ---\n");

	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	printf("--- END PANIC MESSAGE ---\n");
   abort();
}

void __wrap_assert_failed(const char *expr, const char *file, int line)
{
	printf("Kernel assertion '%s' FAILED in %s at line %d\n", expr, file, line);
   abort();
}

void __wrap_not_reached(const char *file, int line)
{
   printf("Kernel NOT_REACHED statement in %s at line %d\n", file, line);
   abort();
}

void init_serial_port() { }
void write_serial() { }
void handle_fault() { }
void handle_syscall() { }
void handle_irq() { }
void pic_send_eoi() { }
void task_info_reset_kernel_stack() { }
void set_kernel_stack() { }
void irq_clear_mask() { }
void kthread_create() { }
void debug_qemu_turn_off_machine() { }
void load_elf_program() { }
void create_usermode_task() { }
void gdt_install() { }
void idt_install() { }
void irq_install() { }
void set_timer_freq() { }
void irq_install_handler() { }
void setup_sysenter_interface() { }
void save_current_task_state() { }
void switch_to_task() { }
