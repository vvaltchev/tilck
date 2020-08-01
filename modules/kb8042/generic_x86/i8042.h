/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>

/* i8042's I/O ports */

#define I8042_DATA_PORT     0x60       /* 0x60 is a read/write data port */
#define I8042_CMD_PORT      0x64       /* 0x64 is for cmds, when it's written */
#define I8042_STATUS_PORT   0x64       /* 0x64 is for status, when it's read */

/* Status flags */

#define I8042_STATUS_OUTPUT_FULL       (1 << 0) /* must be 1 before reading */
#define I8042_STATUS_INPUT_FULL        (1 << 1) /* must be 0 before writing */


/* CTR (Controller Configuration Byte) flags */

#define I8042_CTR_PORT1_INT_ENABLED    (1 << 0)
#define I8042_CTR_PORT2_INT_ENABLED    (1 << 1)
#define I8042_CTR_SYS_FLAG             (1 << 2)
#define I8042_CTR_PORT1_CLK_DISABLED   (1 << 4)
#define I8042_CTR_PORT2_CLK_DISABLED   (1 << 5)
#define I8042_CTR_PS2_PORT_TRANSL      (1 << 6)

/* PS/2 Controller commands */

#define I8042_CMD_CPU_RESET                0xFE
#define I8042_CMD_RESET                    0xFF
#define I8042_CMD_SELFTEST                 0xAA
#define I8042_CMD_PORT1_DISABLE            0xAD
#define I8042_CMD_PORT1_ENABLE             0xAE
#define I8042_CMD_PORT2_DISABLE            0xA7
#define I8042_CMD_PORT2_ENABLE             0xA8
#define I8042_CMD_READ_CTR                 0x20 /* Controller Config. Byte */
#define I8042_CMD_READ_CTO                 0xD0 /* Controller Output Port */

/* PS/2 Controller response codes */
#define I8042_RESPONSE_BAT_OK              0xAA
#define I8042_RESPONSE_SELF_TEST_OK        0x55

/* PS/2 Keyboard commands */
#define KB_CMD_SET_LED                     0xED
#define KB_CMD_SET_TYPEMATIC_BYTE          0xF3

/* PS/2 Keyboard response codes */
#define KB_RESPONSE_ACK                    0xFA
#define KB_RESPONSE_RESEND                 0xFE

/* Logical state functions */
void i8042_set_sw_port_enabled_state(u8 port, bool enabled);
bool i8042_get_sw_port_enabled_state(u8 port);

/* HW keyboard-specific functions */
bool kb_led_set(u8 val);
bool kb_set_typematic_byte(u8 val);

/* HW controller functions */
bool i8042_read_ctr_unsafe(u8 *ctr);
bool i8042_read_cto_unsafe(u8 *cto);
void i8042_drain_any_data(void);
void i8042_force_drain_data(void);
bool i8042_read_regs(u8 *ctr, u8 *cto);
void i8042_reboot(void);

NODISCARD bool i8042_self_test(void);
NODISCARD bool i8042_reset(void);
NODISCARD bool i8042_disable_ports(void);
NODISCARD bool i8042_enable_ports(void);

static inline u8 i8042_read_status(void)
{
   return inb(I8042_STATUS_PORT);
}

static inline u8 i8042_read_data(void)
{
   return inb(I8042_DATA_PORT);
}

static inline NODISCARD bool
i8042_has_pending_data(void)
{
   return !!(inb(I8042_STATUS_PORT) & I8042_STATUS_OUTPUT_FULL);
}

static inline NODISCARD bool
i8042_is_ready_for_cmd(void)
{
   return !(inb(I8042_STATUS_PORT) & I8042_STATUS_INPUT_FULL);
}
