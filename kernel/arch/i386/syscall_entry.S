# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/config_mm.h>

#include <tilck/kernel/arch/i386/asm_defs.h>

.code32

.section .text

.global syscall_int80_entry
.global sysenter_entry
.global context_switch
.global soft_interrupt_resume
.global sysenter_resume

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
    * available. Tilck does not have a feature like VDSO therefore, applications
    * have to explicitly use this convention in order to sysenter to work.
    */

   push 0xcafecafe   # SS: unused for sysenter context regs
   push 0xcafecafe   # ESP: unused for sysenter context regs
   pushf
   push 0xcafecafe   # CS: unused for sysenter context regs
   push 0xcafecafe   # EIP: unused for sysenter context regs

   push 0            # unused "err_code"
   push 0x80

   kernel_entry_common
   push_custom_flags (REGS_FL_SYSENTER)

   push offset sysenter_resume
   mov eax, esp
   cld            # set DF = 0, as C compilers by default assume that.
   push eax
   call syscall_entry

   add esp, 8     # skip the previousy-pushed 'eax' and kernel_resume_eip

sysenter_resume:

   pop_custom_flags
   resume_base_regs

   add esp, 16   # skip err_code and int_num, eip, cs
   popf
   add esp, 8    # skip esp, SS

   mov ecx, ebp  # ecx = user esp (which is saved in ebp)
   mov edx, [sysexit_user_code_user_vaddr]
   sti
   sysexit

END_FUNC(sysenter_entry)

FUNC(syscall_int80_entry):

   push 0
   push 0x80

   kernel_entry_common
   push_custom_flags (0)

   push offset soft_interrupt_resume
   mov eax, esp
   cld            # Set DF = 0, as C compilers by default assume that.
   push eax
   call syscall_entry

   add esp, 8     # Discard the previousy-pushed 'eax' and kernel_resume_eip

soft_interrupt_resume:
   pop_custom_flags
   kernel_exit_common

END_FUNC(syscall_int80_entry)

FUNC(context_switch):

   add esp, 4 # Discard the return-addr.
   pop esp    # Make ESP = function's 1st (and only) argument: regs *contex.
   ret        # Now at [esp] now there's the value of `kernel_resume_eip`.
              # By default, that value is `soft_interrupt_resume` but changes
              # in special occasions (e.g. sysenter/sysexit).

END_FUNC(context_switch)

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
