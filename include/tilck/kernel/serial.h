/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

void init_serial_port(u16 port);

bool serial_received(u16 port);
void serial_wait_for_input_buf(u16 port);
char serial_read(u16 port);

bool serial_transmitted(u16 port);
void serial_wait_for_output_buf(u16 port);
void serial_write(u16 port, char c);
