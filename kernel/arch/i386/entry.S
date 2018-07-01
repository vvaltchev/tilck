
.intel_syntax noprefix

#define ASM_FILE 1

#include <common/config.h>
#include <exos/arch/i386/asm_defs.h>
#include <multiboot.h>

.code32

.section bss
.global kernel_initial_stack
.global vsdo_like_page

.align 4096
.lcomm kernel_initial_stack, KERNEL_INITIAL_STACK_SIZE
.lcomm vsdo_like_page, 4096

.section .text

.global _start
.global asm_enable_sse
.global asm_enable_osxsave
.global asm_enable_avx
.global asm_kernel_yield
.global panic_save_current_state
.global switch_to_initial_kernel_stack

#define MULTIBOOT_FLAGS      (MULTIBOOT_PAGE_ALIGN |   \
                              MULTIBOOT_MEMORY_INFO)

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

   .long 0 /* mode_type: unset */
   .long 0 /* cols: unset */
   .long 0 /* rows: unset */
   .long 0 /* color depth: unset */

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


# This function initialize both the x87 FPU and SSE
FUNC(asm_enable_sse):

   mov eax, cr0
   and eax, ~(1 << 2) # set CR0.EM = 0 [coprocessor emulation]
   and eax, ~(1 << 3) # set CR0.TS = 0 [Task switched]
   or eax, (1 << 1)   # set CR0.MP = 1 [coprocessor monitoring]
   or eax, (1 << 5)   # set CR0.NE = 1 [Native Exception handling]
   mov cr0, eax
   fninit

   mov eax, cr4
   or eax, (1 << 9)   # set CR4.OSFXSR
   or eax, (1 << 10)  # set CR4.OSXMMEXCPT
   mov cr4, eax

   ret

END_FUNC(asm_enable_sse)

FUNC(asm_enable_osxsave):

   mov eax, cr4
   or eax, (1 << 18)  # Enable XSAVE and Processor Extended States (XCR{n})
   mov cr4, eax

   ret

END_FUNC(asm_enable_osxsave)

FUNC(asm_enable_avx):

   xor ecx, ecx
   xgetbv            # Load XCR0 in eax
   or eax, (1 << 0)  # FPU/MMX x87 enabled [must be 1]
   or eax, (1 << 1)  # Set SSE enabled     [can be 0 if AVX is 0]
   or eax, (1 << 2)  # Set AVX enabled     [must be 1, if SSE is 1]
   xsetbv            # Save eax back to XCR0
   ret

END_FUNC(asm_enable_avx)

# Saves the current (kernel) state as if an interrupt occurred while running
# in kernel mode.

FUNC(asm_kernel_yield):

   lock inc dword ptr [disable_preemption_count]

   pushf             # push EFLAGS
   sub esp, 16       # skip cs, eip, err_code, int_num

   pusha             # Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
                     # Note: the value of the pushed ESP is the one before
                     # the pusha instruction.
   push ds
   push es
   push fs
   push gs

   push offset .kernel_yield_resume
   mov eax, esp

   push eax
   call save_current_task_state

#if defined(DEBUG) && KERNEL_TRACK_NESTED_INTERRUPTS
   call check_not_in_irq_handler
#endif

   call schedule_outside_interrupt_context

   # schedule() just returned: restore ESP and just return

   add esp, SIZEOF_REGS - 8 + 4 # -8 : we skipped ss, useresp
                                # +4 : compensate the last push eax above

   lock dec dword ptr [disable_preemption_count]
   ret

.kernel_yield_resume:
   pop gs
   pop fs
   pop es
   pop ds
   popa
   add esp, 16     # Discard int_num, err_code, eip, cs
   popf
   ret

END_FUNC(asm_kernel_yield)

FUNC(panic_save_current_state):

   push ss
   push 0xcafebabe   # placeholder for useresp
   pushf             # push EFLAGS
   push cs
   push 0xcafecafe   # placeholder for eip

   push 0            # err_code
   push -1           # int_num

   pusha             # Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
                     # Note: the value of the pushed ESP is the one before
                     # the pusha instruction.
   push ds
   push es
   push fs
   push gs

   push offset .kernel_yield_resume
   mov eax, esp

   mov ecx, [esp + SIZEOF_REGS]             # ecx = caller's EIP
   mov [eax + REGS_EIP_OFF], ecx            # regs->eip = ecx

   lea ecx, [esp + SIZEOF_REGS]
   mov [eax + REGS_USERESP_OFF], ecx        # regs->useresp = ecx

   push eax
   call panic_save_current_task_state
   add esp, SIZEOF_REGS + 4                 # +4 because the last push eax here
   ret

END_FUNC(panic_save_current_state)

FUNC(switch_to_initial_kernel_stack):

   pop eax # save the return-addr in eax
   mov esp, offset kernel_initial_stack + KERNEL_INITIAL_STACK_SIZE - 4
   push eax
   ret

END_FUNC(switch_to_initial_kernel_stack)



.global __asm_fpu_cpy_single_256_nt

FUNC(__asm_fpu_cpy_single_256_nt):
   jmp memcpy_single_256_failsafe
   .space 128
END_FUNC(__asm_fpu_cpy_single_256_nt)


.global __asm_fpu_cpy_single_256_nt_read

FUNC(__asm_fpu_cpy_single_256_nt_read):
   jmp memcpy_single_256_failsafe
   .space 128
END_FUNC(__asm_fpu_cpy_single_256_nt_read)
