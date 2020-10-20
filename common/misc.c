/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/gfx.h>

struct display_resolution {
   u32 width;
   u32 height;
};

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
