/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/boot/gfx.h>

#if defined(__TILCK_BOOTLOADER__) || defined(__TILCK_EFI_BOOTLOADER__)

#ifdef __TILCK_EFI_BOOTLOADER__
   #undef ASSERT
   #include <efi.h>
   #include <efilib.h>
   #define printk(fmt, ...) Print(CONCAT(L, fmt), ##__VA_ARGS__)
#else
   #include <tilck/common/printk.h>
#endif

bool is_tilck_usable_resolution(u32 w, u32 h)
{
   return w >= TILCK_MIN_RES_X && h >= TILCK_MIN_RES_Y;
}

bool is_tilck_known_resolution(u32 w, u32 h)
{
   if (!is_tilck_usable_resolution(w, h))
      return false;

   if (w % 8) {

      /*
       * Tilck's fb console won't be able to use the optimized functions in this
       * case (they ultimately use a 256-bit wide memcpy()).
       */
      return false;
   }

   return true;
}

bool is_tilck_default_resolution(u32 w, u32 h)
{
   return w == PREFERRED_GFX_MODE_W && h == PREFERRED_GFX_MODE_H;
}

static bool
exists_mode_in_array(video_mode_t mode, video_mode_t *arr, int array_sz)
{
   for (int i = 0; i < array_sz; i++)
      if (arr[i] == mode)
         return true;

   return false;
}

void
filter_video_modes(const struct bootloader_intf *intf,
                   video_mode_t *all_modes,
                   int all_modes_cnt,
                   void *opaque_mi,
                   bool show_modes,
                   int bpp,
                   int ok_modes_start,
                   struct ok_modes_info *okm,
                   void *ctx)
{
   struct generic_video_mode_info gi;
   u32 p, max_mode_pixels = 0;
   video_mode_t max_mode = 0;
   int cnt = ok_modes_start;
   video_mode_t curr_mode_num;

   okm->defmode = INVALID_VIDEO_MODE;

   for (int i = 0; i < all_modes_cnt; i++) {

      curr_mode_num = all_modes ? all_modes[i] : (video_mode_t)i;

      if (!intf->get_mode_info(ctx, curr_mode_num, opaque_mi, &gi))
         continue;

      if (!intf->is_mode_usable(ctx, opaque_mi))
         continue;

      if (gi.bpp != bpp)
         continue;

      if (!is_tilck_usable_resolution(gi.xres, gi.yres))
         continue;

      p = gi.xres * gi.yres;

      if (p > max_mode_pixels) {
         max_mode_pixels = p;
         max_mode = curr_mode_num;
      }

      if (!is_tilck_known_resolution(gi.xres, gi.yres))
         continue;

      if (is_tilck_default_resolution(gi.xres, gi.yres))
         okm->defmode = curr_mode_num;

      if (cnt < okm->ok_modes_array_size - 1) {

         if (show_modes)
            intf->show_mode(ctx, cnt, opaque_mi, okm->defmode == curr_mode_num);

         okm->ok_modes[cnt++] = curr_mode_num;
      }
   }

   if (max_mode_pixels) {

      if (!exists_mode_in_array(max_mode, okm->ok_modes, cnt)) {

         if (!intf->get_mode_info(ctx, max_mode, opaque_mi, &gi))
            panic("get_mode_info(0x%x) failed", max_mode);

         if (show_modes)
            intf->show_mode(ctx, cnt, opaque_mi, false);

         okm->ok_modes[cnt++] = max_mode;

         if (okm->defmode == INVALID_VIDEO_MODE)
            okm->defmode = max_mode;
      }
   }

   if (okm->defmode == INVALID_VIDEO_MODE) {
      if (ok_modes_start > 0)
         okm->defmode = okm->ok_modes[0];
   }

   okm->ok_modes_cnt = cnt;
}

video_mode_t
get_user_video_mode_choice(const struct bootloader_intf *intf,
                           struct ok_modes_info *okm)
{
   int len, err = 0;
   char buf[16];
   long s;

   while (true) {

      printk("Select a video mode [0 - %d]: ", okm->ok_modes_cnt - 1);

      len = intf->read_line(buf, sizeof(buf));

      if (!len) {
         printk("<default>\n");
         return okm->defmode;
      }

      s = tilck_strtol(buf, NULL, 10, &err);

      if (err || s < 0 || s > okm->ok_modes_cnt - 1) {
         printk("Invalid selection.\n");
         continue;
      }

      break;
   }

   return okm->ok_modes[s];
}

#endif // defined(__TILCK_BOOTLOADER__) || defined(__TILCK_EFI_BOOTLOADER__)
