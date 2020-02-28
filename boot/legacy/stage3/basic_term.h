/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

void init_bt(void);
void bt_write_char(char c);
void bt_setcolor(u8 color);
void bt_movecur(int row, int col);
u16 bt_get_curr_row(void);
u16 bt_get_curr_col(void);
