/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/boot/common.h>

#define TILCK_MIN_RES_X                               640
#define TILCK_MIN_RES_Y                               480

#define INVALID_VIDEO_MODE             ((video_mode_t)~0)

struct ok_modes_info {

   video_mode_t *ok_modes;
   int ok_modes_array_size;
   int ok_modes_cnt;
   video_mode_t defmode;
};

bool is_tilck_known_resolution(u32 w, u32 h);
bool is_tilck_default_resolution(u32 w, u32 h);
bool is_tilck_usable_resolution(u32 w, u32 h);

void
filter_video_modes(video_mode_t *all_modes,
                   int all_modes_cnt,
                   void *opaque_mode_info_buf,
                   bool show_modes,
                   int bpp,
                   int ok_modes_start,
                   struct ok_modes_info *okm,
                   void *ctx);

video_mode_t
get_user_video_mode_choice(struct ok_modes_info *okm);
