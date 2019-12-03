/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

void init_serial_port(u16 port);

bool serial_read_ready(u16 port);
void serial_wait_for_read(u16 port);
char serial_read(u16 port);

bool serial_write_ready(u16 port);
void serial_wait_for_write(u16 port);
void serial_write(u16 port, char c);

void early_init_serial_ports(void);
