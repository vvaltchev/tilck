
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/color_defs.h>

#include <exos/kernel/fs/exvfs.h>
#include <exos/kernel/fs/devfs.h>
#include <exos/kernel/errno.h>
#include <exos/kernel/kmalloc.h>
#include <exos/kernel/term.h>

#include <termios.h>      // system header

#include "term_int.h"
#include "tty_int.h"

u8 tty_curr_color = make_color(COLOR_WHITE, COLOR_BLACK);

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   size = MIN(size, MB - 1); /* term_write's size is limited to 2^20 - 1 */
   term_write(buf, size, tty_curr_color);
   return size;
}

/* ----------------- Driver interface ---------------- */

int tty_ioctl(fs_handle h, uptr request, void *argp);

static int tty_create_device_file(int minor, file_ops *ops, devfs_entry_type *t)
{
   *t = DEVFS_CHAR_DEVICE;

   bzero(ops, sizeof(file_ops));

   ops->read = tty_read;
   ops->write = tty_write;
   ops->ioctl = tty_ioctl;
   ops->seek = NULL; /* seek() support is NOT mandatory, of course */
   return 0;
}

void init_tty(void)
{
   c_term = default_termios;
   driver_info *di = kmalloc(sizeof(driver_info));
   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   int major = register_driver(di);
   int rc = create_dev_file("tty", major, 0 /* minor */);

   if (rc != 0)
      panic("TTY: unable to create /dev/tty (error: %d)", rc);

   tty_input_init();
   term_set_filter_func(tty_term_write_filter, &term_write_filter_ctx);
}
