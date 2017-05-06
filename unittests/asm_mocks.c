
// Just defining some necessary symbols for making the linker happy.

void isr0() { }
void isr1() { }
void isr2() { }
void isr3() { }
void isr4() { }
void isr5() { }
void isr6() { }
void isr7() { }
void isr8() { }
void isr9() { }
void isr10() { }
void isr11() { }
void isr12() { }
void isr13() { }
void isr14() { }
void isr15() { }
void isr16() { }
void isr17() { }
void isr18() { }
void isr19() { }
void isr20() { }
void isr21() { }
void isr22() { }
void isr23() { }
void isr24() { }
void isr25() { }
void isr26() { }
void isr27() { }
void isr28() { }
void isr29() { }
void isr30() { }
void isr31() { }
void isr128() { }

void irq0() { }
void irq1() { }
void irq2() { }
void irq3() { }
void irq4() { }
void irq5() { }
void irq6() { }
void irq7() { }
void irq8() { }
void irq9() { }
void irq10() { }
void irq11() { }
void irq12() { }
void irq13() { }
void irq14() { }
void irq15() { }

void idt_load() { }
void gdt_load() { }
void tss_flush() { }
void switch_to_usermode_asm(void *entryPoint, void *stackAddr) { }

void asm_context_switch_x86() { }
void asm_kernel_context_switch_x86() { }
void handle_fault() { }
void handle_syscall() { }
void handle_irq() { }
void PIC_sendEOI() { }
void task_info_reset_kernel_stack() { }
void set_kernel_stack() { }
void set_page_directory() { }
void irq_clear_mask() { }
