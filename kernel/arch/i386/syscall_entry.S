
.intel_syntax noprefix

#define ASM_FILE 1

#include <common/config.h>
#include <exos/arch/i386/asm_defs.h>

.code32

.section .text

.global syscall_int80_entry
.global sysenter_entry
.global asm_sysenter_setup
.global asm_soft_interrupt_entry
.global context_switch

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
