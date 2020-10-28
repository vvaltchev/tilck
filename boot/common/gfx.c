/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include "common_int.h"

#define TILCK_MIN_RES_X                               640
#define TILCK_MIN_RES_Y                               480

struct ok_mode {

   video_mode_t mode;
   struct generic_video_mode_info gi;
};

const struct bootloader_intf *intf;
video_mode_t g_defmode = INVALID_VIDEO_MODE;

static struct ok_mode ok_modes[16];
static int ok_modes_cnt;

static void
append_ok_mode(video_mode_t mode, struct generic_video_mode_info *gi)
{
   if (ok_modes_cnt == ok_modes_cnt - 1)
      return;

   ok_modes[ok_modes_cnt].mode = mode;
   ok_modes[ok_modes_cnt].gi = *gi;
   ok_modes_cnt++;
}

void
init_common_bootloader_code(const struct bootloader_intf *i)
{
   if (!intf)
      intf = i;
}

static bool
is_usable_video_mode(struct generic_video_mode_info *gi)
{
   return gi->xres >= TILCK_MIN_RES_X && gi->yres >= TILCK_MIN_RES_Y;
}

static bool
is_optimal_video_mode(struct generic_video_mode_info *gi)
{
   if (!is_usable_video_mode(gi))
      return false;

   if (gi->xres % 8) {

      /*
       * Tilck's fb console won't be able to use the optimized functions in this
       * case (they ultimately use a 256-bit wide memcpy()).
       */
      return false;
   }

   return true;
}

static bool
is_default_resolution(u32 w, u32 h)
{
   return w == PREFERRED_GFX_MODE_W && h == PREFERRED_GFX_MODE_H;
}

static bool
is_mode_in_ok_list(video_mode_t mode)
{
   for (int i = 0; i < ok_modes_cnt; i++)
      if (ok_modes[i].mode == mode)
         return true;

   return false;
}

void
show_mode(int num, struct generic_video_mode_info *gi, bool is_default)
{
   if (num >= 0)
      printk("Mode [%d]: ", num);

   if (gi->is_text_mode)

      printk("text mode %u x %u%s\n",
             gi->xres, gi->yres, is_default ? " [DEFAULT]" : "");

   else

      printk("%u x %u x %d%s\n",
             gi->xres, gi->yres, gi->bpp, is_default ? " [DEFAULT]" : "");
}

void
show_video_modes(void)
{
   for (int i = 0; i < ok_modes_cnt; i++)
      show_mode(i, &ok_modes[i].gi, ok_modes[i].mode == g_defmode);
}

static video_mode_t
filter_modes_int(video_mode_t *all_modes, int all_modes_cnt, int bpp)
{
   struct generic_video_mode_info gi;
   video_mode_t curr_mode_num;
   video_mode_t max_mode = INVALID_VIDEO_MODE;
   video_mode_t min_mode = INVALID_VIDEO_MODE;
   u32 min_mode_pixels = 0;
   u32 max_mode_pixels = 0;
   u32 p;

   if (intf->text_mode != INVALID_VIDEO_MODE) {

      gi = (struct generic_video_mode_info) {
         .xres = 80,
         .yres = 25,
         .bpp = 0,
         .is_text_mode = true,
      };

      append_ok_mode(intf->text_mode, &gi);
   }

   for (int i = 0; i < all_modes_cnt; i++) {

      curr_mode_num = all_modes ? all_modes[i] : (video_mode_t)i;

      if (!intf->get_mode_info(curr_mode_num, &gi))
         continue;

      if (!gi.is_usable)
         continue;

      if (gi.bpp != bpp)
         continue;

      if (!is_usable_video_mode(&gi))
         continue;

      p = gi.xres * gi.yres;

      if (!min_mode_pixels || p < min_mode_pixels) {
         min_mode_pixels = p;
         min_mode = curr_mode_num;
      }

      if (p > max_mode_pixels) {
         max_mode_pixels = p;
         max_mode = curr_mode_num;
      }

      if (!is_optimal_video_mode(&gi))
         continue;

      if (is_default_resolution(gi.xres, gi.yres))
         g_defmode = curr_mode_num;

      if (ok_modes_cnt < ARRAY_SIZE(ok_modes) - 1)
         append_ok_mode(curr_mode_num, &gi);
   }

   if (g_defmode == INVALID_VIDEO_MODE) {
      g_defmode = min_mode;
   }

   if (max_mode != INVALID_VIDEO_MODE) {

      /* Display the max mode, even if might not be optimal for Tilck */
      if (!is_mode_in_ok_list(max_mode)) {

         if (!intf->get_mode_info(max_mode, &gi))
            panic("get_mode_info(0x%x) failed", max_mode);

         append_ok_mode(max_mode, &gi);

         if (g_defmode == INVALID_VIDEO_MODE)
            g_defmode = max_mode;
      }
   }

   if (g_defmode == INVALID_VIDEO_MODE) {

      if (ok_modes_cnt > 0) {

         g_defmode = ok_modes[0].mode;

         if (g_defmode == intf->text_mode && ok_modes_cnt > 1)
            g_defmode = ok_modes[1].mode;
      }
   }

   return g_defmode;
}

video_mode_t
filter_video_modes(video_mode_t *all_modes, int all_modes_cnt)
{
   if (g_defmode != INVALID_VIDEO_MODE)
      panic("filter_video_modes() called twice");

   filter_modes_int(all_modes, all_modes_cnt, 32);

   if (g_defmode == INVALID_VIDEO_MODE || g_defmode == intf->text_mode) {

      all_modes_cnt = 0;
      g_defmode = INVALID_VIDEO_MODE;
      filter_modes_int(all_modes, all_modes_cnt, 24);

      if (g_defmode == INVALID_VIDEO_MODE)
         g_defmode = intf->get_curr_video_mode();
   }

   return g_defmode;
}

video_mode_t
get_user_video_mode_choice(void)
{
   int len, err = 0;
   char buf[16];
   long s;

   while (true) {

      buf[0] = 0;

      printk("Select a video mode [0 - %d]: ", ok_modes_cnt - 1);
      len = read_line(buf, sizeof(buf));

      if (!len) {
         return g_defmode;
      }

      s = tilck_strtol(buf, NULL, 10, &err);

      if (err || s < 0 || s > ok_modes_cnt - 1) {
         printk("Invalid selection.\n");
         continue;
      }

      break;
   }

   return ok_modes[s].mode;
}

void
fetch_all_video_modes_once(void)
{
   video_mode_t *all_modes = NULL;
   int all_modes_cnt = 0;

   if (g_defmode == INVALID_VIDEO_MODE) {
      intf->get_all_video_modes(&all_modes, &all_modes_cnt);
      filter_video_modes(all_modes, all_modes_cnt);
   }
}

video_mode_t
find_default_video_mode(void)
{
   fetch_all_video_modes_once();
   return g_defmode;
}
