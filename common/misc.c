/* SPDX-License-Identifier: BSD-2-Clause */

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
   {1920, 1080},
};

bool is_tilck_known_resolution(u32 w, u32 h)
{
   const struct display_resolution *kr = tilck_known_resolutions;

   for (u32 i = 0; i < ARRAY_SIZE(tilck_known_resolutions); i++) {
      if (kr[i].width == w && kr[i].height == h)
         return true;
   }

   return false;
}
