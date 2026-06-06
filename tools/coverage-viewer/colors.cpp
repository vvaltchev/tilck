/* SPDX-License-Identifier: BSD-2-Clause */

#include "colors.h"

static bool g_color = false;

bool
colors_enabled()
{
   return g_color;
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
   use_default_colors();   /* -1 == terminal's default fg/bg */

   /* Chrome: white on blue, like the HTML title bar / table heads. */
   init_pair(CVP_TITLE, COLOR_WHITE, COLOR_BLUE);

   /* Coverage cells: black text on the rate color (as in the report). */
   init_pair(CVP_LO,  COLOR_BLACK, COLOR_RED);
   init_pair(CVP_MED, COLOR_BLACK, COLOR_YELLOW);
   init_pair(CVP_HI,  COLOR_BLACK, COLOR_GREEN);

   /* Coverage bar fill: the rate color as foreground (block glyphs). */
   init_pair(CVP_BAR_LO,  COLOR_RED,    -1);
   init_pair(CVP_BAR_MED, COLOR_YELLOW, -1);
   init_pair(CVP_BAR_HI,  COLOR_GREEN,  -1);

   /* Source lines: covered = green text, uncovered = white on red. */
   init_pair(CVP_COVERED,   COLOR_GREEN, -1);
   init_pair(CVP_UNCOVERED, COLOR_WHITE, COLOR_RED);

   /* Function hit counts. */
   init_pair(CVP_FNHI, COLOR_GREEN, -1);
   init_pair(CVP_FNLO, COLOR_WHITE, COLOR_RED);

   /* Selected row: a cyan bar. */
   init_pair(CVP_SEL, COLOR_BLACK, COLOR_CYAN);

   init_pair(CVP_DIM, COLOR_WHITE, -1);
}

chtype
cv_attr(cv_pair p)
{
   if (!g_color) {

      /* Monochrome fallback. */
      switch (p) {
         case CVP_TITLE:     return A_REVERSE;
         case CVP_SEL:       return A_REVERSE;
         case CVP_UNCOVERED: return A_REVERSE;
         case CVP_FNLO:      return A_REVERSE;
         case CVP_LO:        return A_BOLD;
         case CVP_DIM:       return A_DIM;
         default:            return A_NORMAL;
      }
   }

   if (p == CVP_DIM)
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

chtype
bar_attr(bucket b)
{
   switch (b) {
      case bucket::lo:  return cv_attr(CVP_BAR_LO);
      case bucket::med: return cv_attr(CVP_BAR_MED);
      case bucket::hi:  return cv_attr(CVP_BAR_HI);
      case bucket::none: break;
   }

   return cv_attr(CVP_DIM);
}
