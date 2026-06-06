/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include "model.h"

#include <curses.h>

/*
 * Semantic color roles. With a 256-color terminal these map to a muted,
 * low-contrast palette (we deliberately do NOT reproduce the harsh
 * genhtml colors); on a 16-color terminal they fall back to the base
 * ANSI colors, and on a monochrome terminal to plain attributes.
 */
enum cv_pair : short {
   CVP_TITLE = 1,   /* header / chrome band */
   CVP_LO,          /* low coverage (also: uncovered, zero-hit fn) */
   CVP_MED,         /* medium coverage */
   CVP_HI,          /* high coverage (also: covered, hit fn) */
   CVP_DIM,         /* de-emphasized text / rules */
   CVP_TRACK,       /* the unfilled part of a coverage bar */
   CVP_SEL,         /* selected row */
   CVP_ACCENT,      /* small accents: icons, selection marker */
};

/* Set up color pairs (or the monochrome fallback). Call after initscr(). */
void colors_init();

bool colors_enabled();

/* Attribute to OR into output for a given role. */
chtype cv_attr(cv_pair p);

/* Coverage-rate text/bar attribute by bucket. */
chtype bucket_attr(bucket b);
