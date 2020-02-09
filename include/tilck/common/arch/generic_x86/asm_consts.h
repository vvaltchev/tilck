/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once


#define EFLAGS_CF       0x0001
#define EFLAGS_PF       0x0004
#define EFLAGS_AF       0x0010
#define EFLAGS_ZF       0x0040
#define EFLAGS_SF       0x0080
#define EFLAGS_TF       0x0100
#define EFLAGS_IF       0x0200
#define EFLAGS_DF       0x0400
#define EFLAGS_OF       0x0800
#define EFLAGS_NT       0x4000
#define EFLAGS_RF      0x10000
#define EFLAGS_VM      0x20000
#define EFLAGS_AC      0x40000
#define EFLAGS_VIF     0x80000
#define EFLAGS_VIP    0x100000
#define EFLAGS_ID     0x200000

#define EFLAGS_IOPL     0x3000

#define MSR_IA32_SYSENTER_CS            0x174
#define MSR_IA32_SYSENTER_ESP           0x175
#define MSR_IA32_SYSENTER_EIP           0x176

#define MSR_IA32_MTRRCAP                0x0fe
#define MSR_IA32_MTRR_DEF_TYPE          0x2ff


#define MSR_MTRRphysBase0               0x200
#define MSR_MTRRphysMask0               0x201
#define MSR_MTRRphysBase1               0x202
#define MSR_MTRRphysMask1               0x203
#define MSR_MTRRphysBase2               0x204
#define MSR_MTRRphysMask2               0x205
#define MSR_MTRRphysBase3               0x206
#define MSR_MTRRphysMask3               0x207
#define MSR_MTRRphysBase4               0x208
#define MSR_MTRRphysMask4               0x209
#define MSR_MTRRphysBase5               0x20a
#define MSR_MTRRphysMask5               0x20b
#define MSR_MTRRphysBase6               0x20c
#define MSR_MTRRphysMask6               0x20d
#define MSR_MTRRphysBase7               0x20e
#define MSR_MTRRphysMask7               0x20f

#define MSR_IA32_PAT                    0x277

#define CR0_PE              (1u << 0)
#define CR0_MP              (1u << 1)
#define CR0_EM              (1u << 2)
#define CR0_TS              (1u << 3)
#define CR0_ET              (1u << 4)
#define CR0_NE              (1u << 5)

#define CR0_WP              (1u << 16)
#define CR0_AM              (1u << 18)
#define CR0_NW              (1u << 29)
#define CR0_CD              (1u << 30)
#define CR0_PG              (1u << 31)

#define CR4_PSE             (1u << 4)
#define CR4_PAE             (1u << 5)
#define CR4_PGE             (1u << 7)
#define CR4_OSFXSR          (1u << 9)
#define CR4_OSXMMEXCPT      (1u << 10)
#define CR4_OSXSAVE         (1u << 18)

#define XCR0_X87            (1u << 0)
#define XCR0_SSE            (1u << 1)
#define XCR0_AVX            (1u << 2)
