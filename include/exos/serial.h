
#pragma once

void init_serial_port(void);
bool serial_received(void);
bool serial_transmitted(void);

char serial_read(void);
void serial_write(char a);
