
#define TI_F_RESUME_RS_OFF 92 /* offset of: fault_resume_regs */
#define TI_FAULTS_MASK_OFF 96 /* offset of: faults_resume_mask */

/* Some useful asm macros */
#ifdef ASM_FILE

#define FUNC(x) .type x, @function; x
#define END_FUNC(x) .size x, .-(x)

#endif
