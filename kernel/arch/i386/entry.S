
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
.global syscall_int80_entry
.global sysenter_entry
.global asm_sysenter_setup
.global asm_soft_interrupt_entry
.global context_switch
.global kernel_yield
.global panic_save_current_state
.global switch_to_initial_kernel_stack
.global fault_resumable_call

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

   xor ecx, ecx
   xgetbv            # Load XCR0 in eax
   or eax, (1 << 0)  # FPU/MMX x87 enabled
   or eax, (1 << 1)  # Set SSE enabled
   xsetbv            # Save eax back to XCR0
   ret


END_FUNC(asm_enable_osxsave)

FUNC(asm_enable_avx):

   xor ecx, ecx
   xgetbv            # Load XCR0 in eax
   or eax, (1 << 2)  # Set AVX enabled
   xsetbv            # Save eax back to XCR0
   ret

END_FUNC(asm_enable_avx)

FUNC(sysenter_entry):

   /*
    * The following handling of sysenter expects the user code to call sysenter
    * this way:
    *
    * 1. Set eax = syscall number
    * 2. Set ebx, ecx, etc. the parameters like for int 0x80
    * 3. push return_eip
    * 4. push ecx   # save ecx because the kernel will change it
    * 5. push edx   # save edx because the kenrel will change it
    * 6. push ebp   # save ebp because we'll going to use it to store ESP
    * 7. mov ebp, esp
    * 8. sysenter
    *
    * Note: in Linux sysenter is used by the libc through VDSO, when it is
    * available. ExOS does not have a feature like VSDO therefore, applications
    * have to explicitly use this convention in order to sysenter to work.
    */

   push 0xcafecafe   # SS: unused for sysenter context regs
   push 0xcafecafe   # ESP: unused for sysenter context regs
   pushf
   push 0xcafecafe   # CS: unused for sysenter context regs
   push 0xcafecafe   # EIP: unused for sysenter context regs

   push 1           # use 1 as "err_code" to distinguish a context saved
                    # with sysenter from a context saved with int 0x80.
   push 0x80

   pusha
   push ds
   push es
   push fs
   push gs
   mov ax, X86_KERNEL_DATA_SEL
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax

   push offset .sysenter_resume
   mov eax, esp
   cld            # set DF = 0, as C compilers by default assume that.
   push eax
   call soft_interrupt_entry

   add esp, 8     # skip the previousy-pushed 'eax' and kernel_resume_eip

.sysenter_resume:
   pop gs
   pop fs
   pop es
   pop ds
   popa

   add esp, 16   # skip err_code and int_num, eip, cs
   popf
   add esp, 8    # skip esp, SS

   mov ecx, ebp  # ecx = user esp (which is saved in ebp)
   mov edx, USER_VSDO_LIKE_PAGE_VADDR
   sti
   sysexit

END_FUNC(sysenter_entry)

FUNC(asm_sysenter_setup):

   push edi
   push esi

   # Copy the code at sysexit_user_code at vsdo_like_page
   # that will be mapped at user vaddr USER_VSDO_LIKE_PAGE_VADDR.
   # Sysexit will jump to that address when returning to usermode and will
   # do EXACTLY what the Linux kernel does in VSDO after sysexit.

   mov edi, offset vsdo_like_page
   mov esi, offset sysexit_user_code

1:
   mov eax, [esi]
   mov [edi], eax
   add esi, 4
   add edi, 4
   cmp esi, offset sysexit_user_code_end
   jne 1b

   pop esi
   pop edi
   ret

   .align 4
   sysexit_user_code:
   pop ebp
   pop edx
   pop ecx
   ret
   sysexit_user_code_end:

END_FUNC(asm_sysenter_setup)

FUNC(syscall_int80_entry):

   push 0
   push 0x80

   /*
    * No need to jump here because asm_soft_interrupt_entry is right
    * below this function. We're happy to easily skip an useless jump.
    */

END_FUNC(syscall_int80_entry)

# Soft interrupts common entry point
FUNC(asm_soft_interrupt_entry):

   pusha          #  Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
   push ds
   push es
   push fs
   push gs
   mov ax, X86_KERNEL_DATA_SEL
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax

   push offset .soft_interrupt_resume
   mov eax, esp
   cld            # Set DF = 0, as C compilers by default assume that.
   push eax
   call soft_interrupt_entry

   add esp, 8     # Discard the previousy-pushed 'eax' and kernel_resume_eip

.soft_interrupt_resume:
   pop gs
   pop fs
   pop es
   pop ds
   popa
   add esp, 8     # Discards the pushed err_code and int_num
   iret

END_FUNC(asm_soft_interrupt_entry)

FUNC(context_switch):

   add esp, 4 # discard the return-addr
   pop esp

   pop eax
   test eax, eax                  # if (eax == 0) jump .soft_interrupt_resume
   jz .soft_interrupt_resume      # else jump eax
   jmp eax

END_FUNC(context_switch)


# Saves the current (kernel) state as if an interrupt occurred while running
# in kernel mode.

FUNC(kernel_yield):

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

END_FUNC(kernel_yield)

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


FUNC(fault_resumable_call):

   mov ecx, [__current]
   push [ecx + TI_F_RESUME_RS_OFF]   # push current->fault_resume_regs
   push [ecx + TI_FAULTS_MASK_OFF]   # push current->faults_resume_mask

   push ebp
   mov ebp, esp

   push [disable_preemption_count]
   sub esp, 8        # skip 'ss', 'esp'
   pushf
   sub esp, 16       # skip cs, eip, err_code and int_num
   pusha
   sub esp, 16       # skip pushing ds, es, fs, gs
   push offset .asm_fault_resumable_call_resume

   mov ecx, [__current]
   mov [ecx + TI_F_RESUME_RS_OFF], esp

   mov eax, [ebp + EBP_OFFSET_ARG1 + 8]  # arg1: faults_mask
   mov [ecx + TI_FAULTS_MASK_OFF], eax

   mov eax, [ebp + EBP_OFFSET_ARG2 + 8]  # arg2: func
   lea edx, [ebp + EBP_OFFSET_ARG3 + 8]  # &arg3: &nargs
   mov ecx, [edx]                        # arg3: nargs
   shl ecx, 2                            # nargs *= 4

   test ecx, ecx
   jz 2f
1:
   push [edx + ecx]
   sub ecx, 4
   jnz 1b
2:
   call eax
   xor eax, eax      # return value: set to 0 (= no faults)
   leave

   mov ecx, [__current]
   pop [ecx + TI_FAULTS_MASK_OFF]
   pop [ecx + TI_F_RESUME_RS_OFF]
   ret

.asm_fault_resumable_call_resume:

   add esp, 16
   popa
   add esp, 16
   popf
   add esp, 8
   pop [disable_preemption_count]
   leave

   mov ecx, [__current]
   pop [ecx + TI_FAULTS_MASK_OFF]
   pop [ecx + TI_F_RESUME_RS_OFF]
   ret

END_FUNC(fault_resumable_call)
