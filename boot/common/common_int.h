/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/boot/common.h>

extern const struct bootloader_intf *intf;
extern video_mode_t g_defmode;

void show_video_modes(void);
int read_line(char *buf, int buf_sz);
video_mode_t get_user_video_mode_choice(void);
void fetch_all_video_modes_once(void);
