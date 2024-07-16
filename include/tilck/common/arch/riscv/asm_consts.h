/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

/* Status register flags */
#define SR_SIE        0x00000002UL /* Supervisor Interrupt Enable */
#define SR_MIE        0x00000008UL /* Machine Interrupt Enable */
#define SR_SPIE       0x00000020UL /* Previous Supervisor IE */
#define SR_MPIE       0x00000080UL /* Previous Machine IE */
#define SR_SPP        0x00000100UL /* Previously Supervisor */
#define SR_MPP        0x00001800UL /* Previously Machine */
#define SR_SUM        0x00040000UL /* Supervisor User Memory Access */

#define SR_FS         0x00006000UL /* Floating-point Status */
#define SR_FS_OFF     0x00000000UL
#define SR_FS_INITIAL 0x00002000UL
#define SR_FS_CLEAN   0x00004000UL
#define SR_FS_DIRTY   0x00006000UL

#define SR_XS         0x00018000UL /* Extension Status */
#define SR_XS_OFF     0x00000000UL
#define SR_XS_INITIAL 0x00008000UL
#define SR_XS_CLEAN   0x00010000UL
#define SR_XS_DIRTY   0x00018000UL

#if __riscv_xlen == 32
#define SR_SD         0x80000000UL /* FS/XS dirty */
#else
#define SR_SD         0x8000000000000000UL /* FS/XS dirty */
#endif

/* SATP flags */
#if __riscv_xlen == 32
#define SATP_PPN        0x003FFFFFUL
#define SATP_MODE_32    0x80000000UL
#define SATP_MODE       SATP_MODE_32
#define SATP_ASID_BITS  9
#define SATP_ASID_SHIFT 22
#define SATP_ASID_MASK  0x1FFUL
#else
#define SATP_PPN        0x00000FFFFFFFFFFFUL
#define SATP_MODE_39    0x8000000000000000UL
#define SATP_MODE       SATP_MODE_39
#define SATP_ASID_BITS  16
#define SATP_ASID_SHIFT 44
#define SATP_ASID_MASK  0xFFFFUL
#endif

/* Exception cause high bit - is an interrupt if set */
#define CAUSE_IRQ_FLAG  (1UL << (__riscv_xlen - 1UL))

/* Interrupt causes (minus the high bit) */
#define IRQ_S_SOFT      1
#define IRQ_S_TIMER     5
#define IRQ_S_EXT       9

/* Exception causes */
#define EXC_INST_MISALIGNED   0
#define EXC_INST_ACCESS       1
#define EXC_INST_ILLEGAL      2
#define EXC_BREAKPOINT        3
#define EXC_LOAD_MISALIGNED   4
#define EXC_LOAD_ACCESS       5
#define EXC_STORE_MISALIGNED  6
#define EXC_STORE_ACCESS      7
#define EXC_SYSCALL           8
#define EXC_ECALL_S           9
#define EXC_ECALL_H           10
#define EXC_ECALL_M           11
#define EXC_INST_PAGE_FAULT   12
#define EXC_LOAD_PAGE_FAULT   13
#define EXC_RESERVED          14
#define EXC_STORE_PAGE_FAULT  15

#define SYSCALL_SOFT_INTERRUPT   EXC_SYSCALL

/* PMP configuration */
#define PMP_R          0x01
#define PMP_W          0x02
#define PMP_X          0x04
#define PMP_A          0x18
#define PMP_A_TOR      0x08
#define PMP_A_NA4      0x10
#define PMP_A_NAPOT    0x18
#define PMP_L          0x80

/* symbolic CSR names: */
#define CSR_CYCLE      0xc00
#define CSR_TIME       0xc01
#define CSR_INSTRET    0xc02
#define CSR_CYCLEH     0xc80
#define CSR_TIMEH      0xc81
#define CSR_INSTRETH   0xc82

#define CSR_SSTATUS    0x100
#define CSR_SIE        0x104
#define CSR_STVEC      0x105
#define CSR_SCOUNTEREN 0x106
#define CSR_SSCRATCH   0x140
#define CSR_SEPC       0x141
#define CSR_SCAUSE     0x142
#define CSR_STVAL      0x143
#define CSR_SIP        0x144
#define CSR_SATP       0x180

/* IE/IP (Supervisor/Machine Interrupt Enable/Pending) flags */
#define IE_SIE    (0x1UL << IRQ_S_SOFT)
#define IE_TIE    (0x1UL << IRQ_S_TIMER)
#define IE_EIE    (0x1UL << IRQ_S_EXT)
