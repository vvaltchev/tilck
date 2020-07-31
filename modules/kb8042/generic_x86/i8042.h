/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>

#define KB_DATA_PORT        0x60       /* 0x60 is a read/write data port */
#define KB_COMMAND_PORT     0x64       /* 0x64 is for cmds, when it's written */
#define KB_STATUS_PORT      0x64       /* 0x64 is for status, when it's read */

/* Status flags */

#define KB_STATUS_OUTPUT_FULL          (1 << 0) /* must be 1 before reading */
#define KB_STATUS_INPUT_FULL           (1 << 1) /* must be 0 before writing */


/* CTR (Controller Configuration Byte) flags */

#define KB_CTR_PORT1_INT_ENABLED       (1 << 0)
#define KB_CTR_PORT2_INT_ENABLED       (1 << 1)
#define KB_CTR_SYS_FLAG                (1 << 2)
#define KB_CTR_PORT1_CLOCK_DISABLED    (1 << 4)
#define KB_CTR_PORT2_CLOCK_DISABLED    (1 << 5)
#define KB_CTR_PS2_PORT_TRANSLATION    (1 << 6)

/* PS/2 Controller commands */

#define KB_CTRL_CMD_CPU_RESET              0xFE
#define KB_CTRL_CMD_RESET                  0xFF
#define KB_CTRL_CMD_SELFTEST               0xAA
#define KB_CTRL_CMD_PORT1_DISABLE          0xAD
#define KB_CTRL_CMD_PORT1_ENABLE           0xAE
#define KB_CTRL_CMD_PORT2_DISABLE          0xA7
#define KB_CTRL_CMD_PORT2_ENABLE           0xA8
#define KB_CTRL_CMD_READ_CTR               0x20 /* Controller Config. Byte */
#define KB_CTRL_CMD_READ_CTO               0xD0 /* Controller Output Port */

/* Response codes */
#define KB_RESPONSE_ACK                    0xFA
#define KB_RESPONSE_RESEND                 0xFE
#define KB_RESPONSE_BAT_OK                 0xAA
#define KB_RESPONSE_SELF_TEST_OK           0x55

void kb_drain_any_data(void);
void kb_drain_data_no_check(void);
bool kb_led_set(u8 val);
bool kb_set_typematic_byte(u8 val);
bool kb_ctrl_read_ctr_and_cto(u8 *ctr, u8 *cto);
void x86_pc_8042_reboot(void);
NODISCARD bool kb_ctrl_self_test(void);
NODISCARD bool kb_ctrl_reset(void);

static inline u8 kb_ctrl_read_status(void)
{
   return inb(KB_STATUS_PORT);
}

static inline u8 kb_ctrl_read_data(void)
{
   return inb(KB_DATA_PORT);
}

static inline NODISCARD bool kb_ctrl_is_pending_data(void)
{
   return !!(inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL);
}

static inline NODISCARD bool kb_ctrl_is_read_for_next_cmd(void)
{
   return !(inb(KB_STATUS_PORT) & KB_STATUS_INPUT_FULL);
}
