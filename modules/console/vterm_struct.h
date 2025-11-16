/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/term_aux.h>
#include "video_term_int.h"

struct vterm {

   bool initialized;
   bool cursor_enabled;
   bool using_alt_buffer;

   struct term_rb_data rb_data;

   u16 tabsize;               /* term's current tab size */
   u16 rows;                  /* term's rows count */
   u16 cols;                  /* term's columns count */

   u16 col_offset;
   u16 r;                     /* current row */
   u16 c;                     /* current col */

   const struct video_interface *vi;
   const struct video_interface *saved_vi;

   u16 *buffer;               /* the whole screen buffer */
   u16 *screen_buf_copy;      /* when != NULL, contains one screenshot */
   u32 scroll;                /* != max_scroll only while scrolling */
   u32 max_scroll;            /* buffer rows used - rows. Its value is 0 until
                                 the screen scrolls for the first time */
   u32 total_buffer_rows;     /* >= term rows */
   u32 extra_buffer_rows;     /* => total_buffer_rows - rows. Always >= 0 */

   u16 saved_cur_row;         /* keeps primary buffer's cursor's row */
   u16 saved_cur_col;         /* keeps primary buffer's cursor's col */

   u16 main_scroll_region_start;
   u16 main_scroll_region_end;

   u16 alt_scroll_region_start;
   u16 alt_scroll_region_end;

   u16 *start_scroll_region;
   u16 *end_scroll_region;

   bool *tabs_buf;
   bool *main_tabs_buf;
   bool *alt_tabs_buf;

   struct term_action actions_buf[32];

   term_filter filter;
   void *filter_ctx;
};

