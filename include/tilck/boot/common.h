/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#ifdef __TILCK_EFI_BOOTLOADER__
   typedef ulong video_mode_t;
#else
   typedef u16 video_mode_t;
#endif

#define INVALID_VIDEO_MODE             ((video_mode_t)~0)

struct generic_video_mode_info {

   u16 xres;
   u16 yres;
   u8 bpp;
   bool is_text_mode;
   bool is_usable;
};

struct bootloader_intf {

   /* Methods that must be implemented by each bootloader */
   int (*read_key)(void);
   void (*write_char)(char c);
   void (*clear_screen)(void);

   video_mode_t (*get_curr_video_mode)(void);
   bool (*set_curr_video_mode)(video_mode_t);
   void (*get_all_video_modes)(video_mode_t **modes, int *count);
   bool (*get_mode_info)(video_mode_t m, struct generic_video_mode_info *gi);

   /* Const configuration values that must be provided */
   video_mode_t text_mode;
};

void init_common_bootloader_code(const struct bootloader_intf *);
bool common_bootloader_logic(void);
void *simple_elf_loader(void *elf);
video_mode_t find_default_video_mode(void);
