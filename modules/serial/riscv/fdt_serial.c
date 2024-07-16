/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/boot.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/errno.h>
#include <tilck/mods/serial.h>

#define X86_PC_COM1 0x3f8
#define X86_PC_COM2 0x2f8
#define X86_PC_COM3 0x3e8
#define X86_PC_COM4 0x2e8

#define X86_PC_COM2_COM4_IRQ       3
#define X86_PC_COM1_COM3_IRQ       4


void init_serial_port(u16 port)
{
   /* do nothing */
}

bool serial_read_ready(u16 port)
{
   NOT_IMPLEMENTED();
}

void serial_wait_for_read(u16 port)
{
   /* do nothing */
}

bool serial_write_ready(u16 port)
{
   /* do nothing */
   return 0;
}

void serial_wait_for_write(u16 port)
{
   /* do nothing */
}

char serial_read(u16 port)
{
   NOT_IMPLEMENTED();
}

void serial_write(u16 port, char c)
{
   NOT_IMPLEMENTED();
}
