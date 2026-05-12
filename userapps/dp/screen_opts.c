/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Config panel.
 *
 * Where the kernel-side dp_opts.c expanded specific config #defines
 * into a curated build-time view, this port walks the sysfs trees the
 * kernel already populates and lists everything it finds. Less hand-
 * curated, but self-maintaining: any new kernel option that lands
 * under /syst/{config,modules,kernel} or any new kopt under
 * /syst/kopts shows up automatically.
 *
 * Run-time stats moved to a dedicated panel (see screen_runtime.c) so
 * each panel stays focused on one source of data.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "term.h"
#include "tui_layout.h"
#include "dp_int.h"
#include "dp_panel.h"

#define OPT_VAL_MAX  64

static int
read_sysfs_value(const char *path, char *out, size_t out_sz)
{
   int fd = open(path, O_RDONLY);

   if (fd < 0)
      return -1;

   ssize_t n = read(fd, out, out_sz - 1);
   close(fd);

   if (n < 0)
      return -1;

   out[n] = '\0';

   /* Trim trailing whitespace/newline */
   while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' ||
                    out[n-1] == ' '  || out[n-1] == '\t'))
   {
      out[--n] = '\0';
   }

   return 0;
}

/*
 * The kernel's bool ptype writes "0\n" or "1\n"; renderers in dp_opts.c
 * showed bool values in green when set. Mirror that.
 */
static const char *
val_color(const char *val)
{
   if (val[0] == '1' && val[1] == '\0')
      return E_COLOR_GREEN;

   if (val[0] == '0' && val[1] == '\0')
      return TERM_DEFAULT_COLOR;

   return TERM_DEFAULT_COLOR;
}

static void
render_section(const char *dir_path, const char *label, int *row, int col)
{
   DIR *d = opendir(dir_path);
   struct dirent *e;
   char path[512];
   char val[OPT_VAL_MAX];

   dp_write((*row)++, col,
            E_COLOR_BR_WHITE "%s" RESET_ATTRS, label);

   if (!d) {
      dp_write((*row)++, col, "  (missing: %s)", dir_path);
      return;
   }

   while ((e = readdir(d))) {

      if (e->d_name[0] == '.')
         continue;

      snprintf(path, sizeof(path), "%s/%s", dir_path, e->d_name);

      if (read_sysfs_value(path, val, sizeof(val)) < 0)
         strcpy(val, "?");

      dp_write((*row)++, col,
               "  %-26s: %s%s" RESET_ATTRS,
               e->d_name, val_color(val), val);
   }

   closedir(d);

   /* Blank separator between sections */
   (*row)++;
}

static void dp_show_opts(void)
{
   int row = tui_screen_start_row + 1;
   const int col = tui_start_col + 3;

   render_section("/syst/config",  "Build config",   &row, col);
   render_section("/syst/modules", "Modules",        &row, col);
   render_section("/syst/kernel",  "Kernel options", &row, col);
   render_section("/syst/kopts",   "Runtime kopts",  &row, col);
}

static struct dp_screen dp_opts_screen = {
   .index = 0,
   .label = "Config",
   .draw_func = dp_show_opts,
};

__attribute__((constructor))
static void dp_opts_init(void)
{
   dp_register_screen(&dp_opts_screen);
}
