# SPDX-License-Identifier: BSD-2-Clause

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_sched.h>
#include <tilck/kernel/arch/riscv/asm_defs.h>
#include <multiboot.h>

.section .text

.global asm_nop_loop
.global asm_do_bogomips_loop

# Loop used to perform short delays, used by delay_us(). It requires the
# bogoMIPS measurement to be completed in order to be accurate.

FUNC(asm_nop_loop):

.loop:

   ret

END_FUNC(asm_nop_loop)

# Function used to measure Tilck's bogoMIPS, a "bogus" way to measure time which
# stands for "millions of iterations per second". See do_bogomips_loop().
#
# How it works
# ----------------
#
# In measure_bogomips_irq_handler() we'll going to measure the number of
# iterations of the inner loop by calculating: __bogo_loops * BOGOMIPS_CONST.
# Why the double loop? Because __bogo_loops needs to be atomic and atomic
# operations have a significant overhead. To make it negligible, we increment
# __bogo_loops by 1 only once every BOGOMIPS_CONST iterations of the inner loop.
#
# The loop terminates when `__bogo_loops` becomes 0 and that happens when after
# MEASURE_BOGOMIPS_TICKS ticks, measure_bogomips_irq_handler() sets it to -1.
#

FUNC(asm_do_bogomips_loop):

   ret

END_FUNC(asm_do_bogomips_loop)
