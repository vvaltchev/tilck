/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Runtime panel.
 *
 * Mirrors what the master kernel-side dp_opts.c showed in its right-
 * hand "Run-time" column: hypervisor flag, terminal size, framebuffer
 * info, tty count, clock-resync stats. The userspace port keeps these
 * in a dedicated panel rather than re-creating master's two-column
 * Options layout in a single-column form.
 *
 * term_rows/term_cols are tty-relative and come from this process's
 * own TIOCGWINSZ (tui_init_layout). The rest is a single snapshot
 * pulled from the kernel via TILCK_CMD_DP_GET_RUNTIME_INFO.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "term.h"
#include "tui_layout.h"
#include "dp_int.h"
#include "dp_panel.h"

/*
 * Reuse the "0 → default, 1 → green, anything else → default" coloring
 * the Config panel uses for sysfs bools, so the two panels read
 * uniformly.
 */
static const char *
val_color(const char *val)
{
   if (val[0] == '1' && val[1] == '\0')
      return E_COLOR_GREEN;

   return TERM_DEFAULT_COLOR;
}

static void
render_rt_int(int *row, int col, const char *name, unsigned long val)
{
   char vbuf[16];

   snprintf(vbuf, sizeof(vbuf), "%lu", val);
   dp_write((*row)++, col,
            "  %-26s: %s%s" RESET_ATTRS,
            name, val_color(vbuf), vbuf);
}

static void
render_section_label(int *row, int col, const char *label)
{
   dp_write((*row)++, col, E_COLOR_BR_WHITE "%s" RESET_ATTRS, label);
}

static void dp_show_runtime(void)
{
   struct dp_runtime_info ri = {0};
   int row = tui_screen_start_row + 1;
   const int col = tui_start_col + 3;
   long rc;

   rc = syscall(TILCK_CMD_SYSCALL,
                TILCK_CMD_DP_GET_RUNTIME_INFO,
                (long)&ri, 0L, 0L, 0L);

   if (rc < 0) {
      dp_write(row, col,
               "TILCK_CMD_DP_GET_RUNTIME_INFO failed: %ld", rc);
      return;
   }

   render_section_label(&row, col, "Misc");
   render_rt_int(&row, col, "hypervisor",      ri.hypervisor);
   row++;

   render_section_label(&row, col, "Console");
   render_rt_int(&row, col, "term_rows",       (unsigned long)tui_rows);
   render_rt_int(&row, col, "term_cols",       (unsigned long)tui_cols);
   render_rt_int(&row, col, "use_framebuffer", ri.use_framebuffer);

   /*
    * Framebuffer details are inlined under Console rather than getting
    * their own sub-section: they only exist when use_framebuffer is
    * true, and visually they belong with the console/display block.
    */
   if (ri.use_framebuffer) {
      render_rt_int(&row, col, "fb_opt_funcs", ri.fb_opt_funcs);
      render_rt_int(&row, col, "fb_res_x",     ri.fb_res_x);
      render_rt_int(&row, col, "fb_res_y",     ri.fb_res_y);
      render_rt_int(&row, col, "fb_bpp",       ri.fb_bpp);
      render_rt_int(&row, col, "fb_font_w",    ri.fb_font_w);
      render_rt_int(&row, col, "fb_font_h",    ri.fb_font_h);
   }

   render_rt_int(&row, col, "tty_count",       ri.tty_count);
   row++;

   /*
    * Clock-resync stats keep master's short names (clk_full_rs_cnt,
    * clk_full_rs_adg1, ...) rather than expanding them to the
    * underlying struct field names, so each row fits inside the
    * 26-char name column and the colons stay aligned.
    */
   render_section_label(&row, col, "Clock Sync");
   render_rt_int(&row, col, "clk_in_rs",        ri.clk_in_resync);
   render_rt_int(&row, col, "clk_in_full_rs",   ri.clk_in_full_resync);
   render_rt_int(&row, col, "clk_full_rs_cnt",  ri.clk_full_resync_count);
   render_rt_int(&row, col, "clk_full_rs_fail", ri.clk_full_resync_fail_count);
   render_rt_int(&row, col, "clk_full_rs_succ", ri.clk_full_resync_success_count);
   render_rt_int(&row, col, "clk_full_rs_adg1", ri.clk_full_resync_abs_drift_gt_1);
   render_rt_int(&row, col, "clk_resync_cnt",   ri.clk_multi_second_resync_count);
}

static struct dp_screen dp_runtime_screen = {
   .index = 1,
   .label = "Runtime",
   .draw_func = dp_show_runtime,
};

__attribute__((constructor))
static void dp_runtime_init(void)
{
   dp_register_screen(&dp_runtime_screen);
}
