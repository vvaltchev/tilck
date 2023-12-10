/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/tty_struct.h>

STATIC struct tty *
allocate_and_init_tty(u16 minor, u16 serial_port_fwd, int rows_buf);

void tty_update_default_state_tables(struct tty *t);

extern term *__curr_term;
extern const struct term_interface *__curr_term_intf;
extern const struct term_interface *video_term_intf;
extern const struct term_interface *serial_term_intf;
