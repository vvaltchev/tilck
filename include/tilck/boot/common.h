/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define LOADING_INITRD_STR          "Loading ramdisk... "
#define LOADING_BOOTPART_STR        "Loading bootpart... "

#ifdef __TILCK_EFI_BOOTLOADER__

   typedef ulong video_mode_t;
   #define LOADING_INITRD_STR_U     CONCAT(L, LOADING_INITRD_STR)

#else

   typedef u16 video_mode_t;

#endif

#define INVALID_VIDEO_MODE             ((video_mode_t)~0)
#define CMDLINE_BUF_SZ                                 80

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
   void (*set_color)(u8 color);

   video_mode_t (*get_curr_video_mode)(void);
   bool (*set_curr_video_mode)(video_mode_t);
   void (*get_all_video_modes)(video_mode_t **modes, int *count);
   bool (*get_mode_info)(video_mode_t m, struct generic_video_mode_info *gi);

   bool (*load_kernel_file)(const char *path, void **paddr);
   bool (*load_initrd)(void);

   char *(*get_cmdline_buf)(u32 *buf_sz);

   /* Const configuration values that must be provided */
   video_mode_t text_mode;
   bool efi;
};

void init_common_bootloader_code(const struct bootloader_intf *);
bool common_bootloader_logic(void);
video_mode_t find_default_video_mode(void);
void *load_kernel_image(void);
size_t get_loaded_kernel_mem_sz(void);

void write_bootloader_hello_msg(void);
void write_ok_msg(void);
void write_fail_msg(void);
