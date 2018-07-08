
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>

#include <exos/kernel/fs/exvfs.h>
#include <exos/kernel/fs/devfs.h>
#include <exos/kernel/errno.h>
#include <exos/kernel/kmalloc.h>
#include <exos/kernel/sync.h>
#include <exos/kernel/kb.h>
#include <exos/kernel/process.h>
#include <exos/kernel/term.h>
#include <exos/kernel/user.h>
#include <exos/kernel/ringbuf.h>
#include <exos/kernel/kb_scancode_set1_keys.h>

#include <termios.h>      // system header
#include "tty_input.c.h"

void tty_update_special_ctrl_handlers(void);

static int
tty_term_write_filter(char *c,
                      u8 *color,
                      term_int_write_char_func write_char_func,
                      void *ctx)
{
   switch (*c) {

      case '\n':

         if (c_term.c_oflag & (OPOST | ONLCR))
            write_char_func('\r', *color);

         return TERM_FILTER_FUNC_RET_WRITE_C;
   }

   return TERM_FILTER_FUNC_RET_WRITE_C;
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   // NOTE: the 'size' arg passed to term_write cannot be bigger than 1 MB.
   // TODO: call term_write() in a loop in order to handle size > 1 MB.

   term_write(buf, size);
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

   kcond_init(&kb_input_cond);
   ringbuf_init(&kb_input_ringbuf, KB_INPUT_BS, 1, kb_input_buf);

   tty_update_special_ctrl_handlers();

   if (kb_register_keypress_handler(&tty_keypress_handler) < 0)
      panic("TTY: unable to register keypress handler");

   term_set_filter_func(tty_term_write_filter, NULL);
}
