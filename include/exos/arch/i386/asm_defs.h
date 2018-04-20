
#pragma once

#define TI_F_RESUME_RS_OFF 92 /* offset of: fault_resume_regs */
#define TI_FAULTS_MASK_OFF 96 /* offset of: faults_resume_mask */

#define SIZEOF_REGS 80
#define REGS_EIP_OFF 60
#define REGS_USERESP_OFF 72

/* Some useful asm macros */
#ifdef ASM_FILE

#define FUNC(x) .type x, @function; x
#define END_FUNC(x) .size x, .-(x)

#define EBP_OFFSET_ARG1 8
#define EBP_OFFSET_ARG2 12
#define EBP_OFFSET_ARG3 16

#endif
