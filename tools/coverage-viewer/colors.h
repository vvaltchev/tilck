/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include "model.h"

#include <curses.h>

/*
 * Semantic color pairs, mapping the genhtml palette to terminal colors.
 * The exact RGB of the HTML report can't be reproduced on a 16-color
 * terminal, but the *coding* is what matters: red = low / uncovered,
 * yellow = medium, green = high / covered, blue = chrome. Everything
 * degrades to monochrome attributes when the terminal has no colors.
 */
enum cv_pair : short {
   CVP_TITLE = 1,   /* title bar + column headers (chrome) */
   CVP_LO,          /* low coverage cell (red) */
   CVP_MED,         /* medium coverage cell (yellow) */
   CVP_HI,          /* high coverage cell (green) */
   CVP_BAR_LO,      /* coverage bar fill, low */
   CVP_BAR_MED,     /* coverage bar fill, medium */
   CVP_BAR_HI,      /* coverage bar fill, high */
   CVP_COVERED,     /* source line executed */
   CVP_UNCOVERED,   /* source line not executed */
   CVP_FNHI,        /* function hit (count > 0) */
   CVP_FNLO,        /* function missed (count == 0) */
   CVP_SEL,         /* selected row */
   CVP_DIM,         /* de-emphasized text */
};

/* Set up color pairs (or the monochrome fallback). Call after initscr(). */
void colors_init();

bool colors_enabled();

/* Attribute to OR into output for a given semantic role. */
chtype cv_attr(cv_pair p);

/* Coverage-rate cell attribute (text color by bucket). */
chtype bucket_attr(bucket b);

/* Coverage-bar fill attribute by bucket. */
chtype bar_attr(bucket b);
