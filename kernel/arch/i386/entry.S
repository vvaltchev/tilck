
.intel_syntax noprefix

#define ASM_FILE 1

#include <common/config.h>
#include <multiboot.h>

#define KERNEL_INITIAL_STACK_SIZE (4*KB)

.section bss
.align 4096
.lcomm kernel_initial_stack, KERNEL_INITIAL_STACK_SIZE

.section .text

.global _start
.global isr128
.global idt_load
.global asm_int_handler
.global context_switch
.global kernel_yield
.global panic_save_current_state


#define MULTIBOOT_FLAGS      (MULTIBOOT_PAGE_ALIGN |   \
                              MULTIBOOT_MEMORY_INFO |  \
                              MULTIBOOT_VIDEO_MODE)

#define PAGE_DIR_PADDR ((offset page_size_buf) - KERNEL_BASE_VA)

FUNC(_start):

   jmp multiboot_entry

/* Multiboot header */

.align 4
   .long   MULTIBOOT_HEADER_MAGIC
   .long   MULTIBOOT_FLAGS
   .long   -(MULTIBOOT_HEADER_MAGIC+MULTIBOOT_FLAGS) /* checksum */

   .long 0
   .long 0
   .long 0
   .long 0
   .long 0

   .long 1  /* EGA-standard text mode */
   .long 80 /* cols */
   .long 25 /* rows */
   .long 0  /* color depth (0 in text mode) */

/* End multiboot header */

multiboot_entry:

   /* Clear the direction flag */
   cld

   /*
    * Before jump to kernel, we have to setup a basic paging in order to map
    * the first 4-MB both at 0 and at +KERNEL_BASE_VA. Using 4-MB pages.
    * NOTE: the registers EAX and EBX cannot be used since they contain
    * multiboot information!
    */

   mov edi, PAGE_DIR_PADDR
   xor ecx, ecx

   # Zero our page directory
1:
   mov [edi], ecx
   add edi, 4
   cmp edi, PAGE_DIR_PADDR + 4096
   jne 1b

   # Set our flags (note the absence of a paddr since it is 0)
   mov ecx, 1 /* present */ | 2 /* RW */ | (1 << 7) /* 4-MB page */

   mov edx, PAGE_DIR_PADDR

   # Identity map the first 4 MB
   mov [edx], ecx

   # Map the first 4 MB at KERNEL_BASE_VA
   mov [edx + (KERNEL_BASE_VA >> 20)], ecx

   # Set the CR3 register
   mov cr3, edx

   mov ecx, cr4
   or ecx, 16      # bit 4, PSE (Page Size Extension). Allows 4-MB pages.
   or ecx, 128     # bit 7, PGE (Page Global Enabled)
   mov cr4, ecx

   mov ecx, cr0
   or ecx, 0x80000000 # paging ON
   or ecx, 0x00010000 # WP ON (write protect for supervisor)
   mov cr0, ecx       # enable paging!


   mov ecx, offset .next_step
   jmp ecx        # Jump to next instruction using the high virtual address.

                  # This is necessary since here the EIP is still a physical
                  # address, while in the kernel the physical identity mapping
                  # is removed. We need to continue using high (+3 GB)
                  # virtual addresses. The trick works because this file is
                  # part of the kernel ELF binary where the ORG is set to
                  # 0xC0100000 (KERNEL_BASE_VA + KERNEL_PADDR).

.next_step:
   mov esp, offset kernel_initial_stack + KERNEL_INITIAL_STACK_SIZE - 4

   push ebx    # 2st argument: multiboot information structure
   push eax    # 1nd argument: multiboot magic
   call kmain  # Now call kernel's kmain() which uses
               # KERNEL_BASE_VA + KERNEL_PADDR as ORG

END_FUNC(_start)

FUNC(idt_load):
    lidt [idtp]
    ret
END_FUNC(idt_load)

# int 0x80: syscall
FUNC(isr128):
   push 0
   push 0x80
   jmp asm_int_handler
END_FUNC(isr128)

# Common interrupt handler (exceptions, IRQs, int 0x80, ...)
FUNC(asm_int_handler):

   pusha          #  Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
   push ds
   push es
   push fs
   push gs
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov eax, esp
   push eax
   cld            # Set DF = 0, as C compilers by default assume that.
   call generic_interrupt_handler
   add esp, 4     # Discard the previousy-pushed 'eax'
   pop gs
   pop fs
   pop es
   pop ds
   popa
   add esp, 8     # Discards the pushed err_code and int_num
   iret

END_FUNC(asm_int_handler)

FUNC(context_switch):

   add esp, 4 # discard the return-addr

   pop esp

   pop gs
   pop fs
   pop es
   pop ds

   popa

   add esp, 8 # skip int_num, err_code

   # The following iret will pop in order: eip, cs, eflags, esp, ss in case of a
   # switch to a user task (change of privilege level) and: eip, cs, eflags
   # otherwise (no change in the privilege level).

   iret

END_FUNC(context_switch)



.macro asm_macro_save_current_kernel_state

   pushf             # push EFLAGS
   push cs           # code segment
   push 0xcafebabe   # placeholder for EIP

   push 0            # err_code
   push -1           # int_num

   pusha             # Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
                     # Note: the value of the pushed ESP is the one before
                     # the pusha instruction.
   push ds
   push es
   push fs
   push gs

   mov eax, esp

.endm



# Saves the current (kernel) state as if an interrupt occurred while running
# in kernel mode.

FUNC(kernel_yield):

   inc dword ptr [disable_preemption_count]

   asm_macro_save_current_kernel_state

   mov ebx, [esp + 17*4]  # caller's EIP
   mov [eax + 14*4], ebx  # regs->eip = ebx

   push eax
   call save_current_task_state

#ifdef DEBUG
   call check_not_in_irq_handler
#endif

   call schedule_outside_interrupt_context

   # In case schedule() returned, restore ESP and just return
   add esp, 72
   dec dword ptr [disable_preemption_count]
   ret

END_FUNC(kernel_yield)

FUNC(panic_save_current_state):

   push ss
   push 0xcafebabe # placeholder for useresp

   asm_macro_save_current_kernel_state

   mov ebx, [esp + 19*4]  # caller's EIP
   mov [eax + 14*4], ebx  # regs->eip = ebx

   lea ebx, [esp + 19*4]
   mov [eax + 17*4], ebx  # regs->useresp = ebx

   push eax
   call panic_save_current_task_state
   add esp, 80
   ret

END_FUNC(panic_save_current_state)
