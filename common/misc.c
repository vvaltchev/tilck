/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck/common/basic_defs.h>

struct display_resolution {
   u32 width;
   u32 height;
};

static const struct display_resolution tilck_known_resolutions[] =
{
   {640, 480},
   {800, 600},
   {1024, 600},
   {1024, 768},
   {1280, 1024},
   {1440, 900},
   {1600, 900},
   {1600, 1200},
   {1920, 1080},
};

bool is_tilck_known_resolution(u32 w, u32 h)
{
   const struct display_resolution *kr = tilck_known_resolutions;

   for (int i = 0; i < ARRAY_SIZE(tilck_known_resolutions); i++) {
      if (kr[i].width == w && kr[i].height == h)
         return true;
   }

   return false;
}

bool is_tilck_default_resolution(u32 w, u32 h)
{
   return w == PREFERRED_GFX_MODE_W && h == PREFERRED_GFX_MODE_H;
}
