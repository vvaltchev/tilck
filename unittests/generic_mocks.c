
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <common/basic_defs.h>

bool suppress_printk;

void __wrap_printk(const char *fmt, ...)
{
   if (suppress_printk)
      return;

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

int copy_from_user(void *dest, const void *user_ptr, size_t n)
{
   memcpy(dest, user_ptr, n);
   return 0;
}

int copy_str_from_user(void *dest, const void *user_ptr)
{
   strcpy(dest, user_ptr);
   return 0;
}

int copy_to_user(void *user_ptr, const void *src, size_t n)
{
   memcpy(user_ptr, src, n);
   return 0;
}

int check_user_ptr_size_writable(void *user_ptr)
{
   return 0;
}

int check_user_ptr_size_readable(void *user_ptr)
{
   return 0;
}

int copy_str_array_from_user(void *dest,
                             const char *const *user_arr,
                             size_t max_size, size_t *written_ptr)
{
   NOT_IMPLEMENTED();
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
void timer_set_freq() { }
void irq_install_handler() { }
void setup_sysenter_interface() { }
void save_current_task_state() { }
void switch_to_task() { }
void pdir_clone() { }
void pdir_destroy() { }
void set_page_directory() { }
