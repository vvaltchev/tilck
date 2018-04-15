
.intel_syntax noprefix

#define ASM_FILE 1

#include <common/config.h>
#include <multiboot.h>

.section bss
.global kernel_initial_stack

.align 4096
.lcomm kernel_initial_stack, KERNEL_INITIAL_STACK_SIZE

.section .text

.global _start
.global syscall_int80_entry
.global sysenter_entry
.global asm_soft_interrupt_entry
.global context_switch
.global kernel_yield
.global panic_save_current_state
.global switch_to_initial_kernel_stack

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
    * 7. ebp = esp
    * 8. sysenter
    *
    * Note: in Linux sysenter is used by the libc through VDSO, when it is
    * available. ExOS does not have a feature like VSDO therefore, applications
    * have to explicitly use this convention in order to sysenter to work.
    */

   push 0x23     # SS: user data sel
   push ebp      # user esp
   pushf
   push 0x1B     # CS: user code sel
   push [ebp+12] # eip

   push 0x0      # "err code" (clearly unused in this context)
   push 0x80

   pusha
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
   call soft_interrupt_entry
   add esp, 4     # Discard the previousy-pushed 'eax'
   pop gs
   pop fs
   pop es
   pop ds
   popa

   add esp, 8     # Discards the pushed err_code and int_num

   pop edx     # eip
   add esp, 4  # skip CS
   popf
   pop ecx
   add esp, 4  # skip SS

   sti
   sysexit

END_FUNC(sysenter_entry)

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
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov eax, esp
   push eax
   cld            # Set DF = 0, as C compilers by default assume that.
   call soft_interrupt_entry
   add esp, 4     # Discard the previousy-pushed 'eax'
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

   # When the thread is resumed in context_switch(), we'll be again in this
   # function (form the ESP point-of-view) and caller's EIP will be (as it is
   # now) already on the stack. We just have to set the EIP in a way to do just
   # a 'ret'. For the caller, after the switch, it would be as if kernel_yield()
   # did nothing but just returning.
   mov dword ptr [eax + 14*4], offset .do_ret # regs->eip = .do_ret

   push eax
   call save_current_task_state

#ifdef DEBUG
   call check_not_in_irq_handler
#endif

   call schedule_outside_interrupt_context

   # In case schedule() returned, restore ESP and just return
   add esp, 72
   dec dword ptr [disable_preemption_count]

.do_ret:
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

FUNC(switch_to_initial_kernel_stack):

   pop eax # save the return-addr in eax
   mov esp, offset kernel_initial_stack + KERNEL_INITIAL_STACK_SIZE - 4
   push eax
   ret

END_FUNC(switch_to_initial_kernel_stack)
