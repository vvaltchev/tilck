
#pragma once

#define COM1 0x3f8

void init_serial_port(void);
int serial_received(void);
char read_serial(void);
int is_transmit_empty(void);
void write_serial(char a);
