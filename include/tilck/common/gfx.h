/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define TILCK_MIN_RES_X                            640
#define TILCK_MIN_RES_Y                            480

#ifdef __TILCK_EFI_BOOTLOADER__

   typedef ulong video_mode_t;
   #define INVALID_VIDEO_MODE       0xffffffff

#else

   typedef u16 video_mode_t;
   #define INVALID_VIDEO_MODE       0xffff

#endif

struct generic_video_mode_info {

   u16 xres;
   u16 yres;
   u8 bpp;
};

struct bootloader_intf {

   bool (*get_mode_info)(void *ctx,
                         video_mode_t m,
                         void *opaque_info_buf,
                         struct generic_video_mode_info *gi);

   bool (*is_mode_usable)(void *ctx, void *opaque_info);
   void (*show_mode)(void *ctx, int num, void *opaque_info, bool is_default);
};

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
filter_video_modes(const struct bootloader_intf *intf,
                   video_mode_t *all_modes,
                   int all_modes_cnt,
                   void *opaque_mode_info_buf,
                   bool show_modes,
                   int bpp,
                   int ok_modes_start,
                   struct ok_modes_info *okm,
                   void *ctx);
