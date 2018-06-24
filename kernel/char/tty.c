
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/fs/exvfs.h>
#include <exos/fs/devfs.h>
#include <exos/errno.h>
#include <exos/kmalloc.h>
#include <exos/sync.h>
#include <exos/kb.h>
#include <exos/process.h>
#include <exos/term.h>
#include <exos/user.h>

static ssize_t tty_read(fs_handle h, char *buf, size_t size)
{
   size_t read_count = 0;
   ASSERT(is_preemption_enabled());

   if (!size)
      return read_count;

   term_set_col_offset(term_get_curr_col());

   do {

      while (kb_cbuf_is_empty()) {
         kcond_wait(&kb_cond, NULL, KCOND_WAIT_FOREVER);
      }

      while (read_count < size && !kb_cbuf_is_empty()) {
         buf[read_count++] = kb_cbuf_read_elem();
      }

   } while (buf[read_count - 1] != '\n' || kb_cbuf_is_full());

   return read_count;
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   // NOTE: the 'size' arg passed to term_write cannot be bigger than 1 MB.
   // TODO: call term_write() in a loop in order to handle size > 1 MB.

   term_write(buf, size);
   return size;
}

/* -------------- TTY ioctl ------------- */

#include <termios.h>      // system header
#include <sys/ioctl.h>    // system header

static const struct termios hard_coded_termios =
{
   0x4500,
   0x05,
   0xbf,
   0x8a3b,
   0,
   {
      0x3, 0x1c, 0x7f, 0x15, 0x4, 0x0, 0x1, 0x0,
      0x11, 0x13, 0x1a, 0x0, 0x12, 0xf, 0x17, 0x16,
      0x0, 0x0, 0x0
   },
   0, // ispeed
   0  // ospeed
};

static int tty_ioctl(fs_handle h, uptr request, void *argp)
{
   //printk("tty_ioctl(request: %p)\n", request);

   if (request == TCGETS) {

      int rc = copy_to_user(argp, &hard_coded_termios, sizeof(struct termios));

      if (rc < 0)
         return -EFAULT;

      return 0;
   }

   if (request == TIOCGWINSZ) {

      struct winsize sz = {
         .ws_row = term_get_rows(),
         .ws_col = term_get_cols(),
         .ws_xpixel = 0,
         .ws_ypixel = 0
      };

      int rc = copy_to_user(argp, &sz, sizeof(struct winsize));

      if (rc < 0)
         return -EFAULT;

      return 0;
   }

   printk("WARNING: unknown tty_ioctl() request: %p\n", request);
   return -EINVAL;
}

/* ----------------- Driver interface ---------------- */

static int tty_create_device_file(int minor, file_ops *ops)
{
   (void)minor; /* ignored */

   bzero(ops, sizeof(file_ops));

   ops->fread = tty_read;
   ops->fwrite = tty_write;
   ops->fseek = NULL;
   ops->ioctl = tty_ioctl;
   ops->fstat = NULL; /* TODO: implement this */

   return 0;
}

void init_tty(void)
{
   driver_info *di = kmalloc(sizeof(driver_info));
   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   int major = register_driver(di);
   create_dev_file("tty", major, 0);
}
