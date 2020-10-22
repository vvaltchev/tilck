/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#ifdef __TILCK_EFI_BOOTLOADER__
   typedef ulong video_mode_t;
#else
   typedef u16 video_mode_t;
#endif


struct generic_video_mode_info {

   u16 xres;
   u16 yres;
   u8 bpp;
};

struct bootloader_intf {

   bool (*get_mode_info)(video_mode_t m,
                         void *opaque_info_buf,
                         struct generic_video_mode_info *gi);

   bool (*is_mode_usable)(void *opaque_info);
   int (*read_key)(void);
   void (*write_char)(char c);
};

void init_common_bootloader_code(const struct bootloader_intf *);
void *simple_elf_loader(void *elf);
int read_line(char *buf, int buf_sz);
