
#pragma once

#define TI_F_RESUME_RS_OFF 92 /* offset of: fault_resume_regs */
#define TI_FAULTS_MASK_OFF 96 /* offset of: faults_resume_mask */

#define SIZEOF_REGS 84
#define REGS_EIP_OFF 64
#define REGS_USERESP_OFF 76

#define REGS_FL_SYSENTER     1
#define REGS_FL_FPU_ENABLED  8

#define X86_KERNEL_CODE_SEL 0x08
#define X86_KERNEL_DATA_SEL 0x10
#define X86_USER_CODE_SEL   0x1b
#define X86_USER_DATA_SEL   0x23

/* Some useful asm macros */
#ifdef ASM_FILE

#define FUNC(x) .type x, @function; x
#define END_FUNC(x) .size x, .-(x)

#define EBP_OFFSET_ARG1  8
#define EBP_OFFSET_ARG2 12
#define EBP_OFFSET_ARG3 16

#endif
