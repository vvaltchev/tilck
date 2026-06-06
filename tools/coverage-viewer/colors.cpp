/* SPDX-License-Identifier: BSD-2-Clause */

#include "colors.h"

static bool g_color = false;

bool
colors_enabled()
{
   return g_color;
}

/*
 * Muted xterm-256 palette. Chosen to be easy on the eyes and cohesive
 * rather than to match the (harsh) HTML report:
 *
 *   low    167  dusty red        title bg 60   soft indigo
 *   med    179  soft amber       title fg 254
 *   high   108  sage green       sel bg   24   deep blue
 *   dim    240  gray             sel fg   254
 *   accent 110  soft sky-blue
 */
static void
init_256()
{
   init_pair(CVP_TITLE, 254, 60);
   init_pair(CVP_LO, 167, -1);
   init_pair(CVP_MED, 179, -1);
   init_pair(CVP_HI, 108, -1);
   init_pair(CVP_DIM, 240, -1);
   init_pair(CVP_TRACK, 236, -1);   /* faint grey bar track */
   init_pair(CVP_SEL, 254, 24);
   init_pair(CVP_ACCENT, 110, -1);
}

static void
init_16()
{
   init_pair(CVP_TITLE, COLOR_WHITE, COLOR_BLUE);
   init_pair(CVP_LO, COLOR_RED, -1);
   init_pair(CVP_MED, COLOR_YELLOW, -1);
   init_pair(CVP_HI, COLOR_GREEN, -1);
   init_pair(CVP_DIM, COLOR_WHITE, -1);
   init_pair(CVP_TRACK, COLOR_BLACK, -1);
   init_pair(CVP_SEL, COLOR_WHITE, COLOR_BLUE);
   init_pair(CVP_ACCENT, COLOR_CYAN, -1);
}

void
colors_init()
{
   if (!has_colors()) {
      g_color = false;
      return;
   }

   g_color = true;
   start_color();
   use_default_colors();

   if (COLORS >= 256)
      init_256();
   else
      init_16();
}

chtype
cv_attr(cv_pair p)
{
   if (!g_color) {
      switch (p) {
         case CVP_TITLE: return A_REVERSE;
         case CVP_SEL:   return A_REVERSE;
         case CVP_LO:    return A_BOLD;
         case CVP_DIM:   return A_DIM;
         case CVP_TRACK: return A_DIM;
         default:        return A_NORMAL;
      }
   }

   if (p == CVP_DIM && COLORS < 256)
      return COLOR_PAIR(p) | A_DIM;

   return COLOR_PAIR(p);
}

chtype
bucket_attr(bucket b)
{
   switch (b) {
      case bucket::lo:  return cv_attr(CVP_LO);
      case bucket::med: return cv_attr(CVP_MED);
      case bucket::hi:  return cv_attr(CVP_HI);
      case bucket::none: break;
   }

   return cv_attr(CVP_DIM);
}
