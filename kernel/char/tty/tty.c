/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/tasklet.h>

#include <linux/major.h> // system header

#include "tty_int.h"

#define TTYS0_MINOR 64

STATIC_ASSERT(TTY_COUNT <= MAX_TTYS);

struct tty *ttys[128];
struct tty *__curr_tty;
int tty_tasklet_runner;
static const struct video_interface *first_term_initial_vi;

static struct keypress_handler_elem tty_keypress_handler_elem =
{
   .handler = &tty_keypress_handler
};

STATIC_ASSERT(ARRAY_SIZE(ttys) > MAX_TTYS);

static ssize_t tty_read(fs_handle h, char *buf, size_t size)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_read_int(t, dh, buf, size);
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_write_int(t, dh, buf, size);
}

static int tty_ioctl(fs_handle h, uptr request, void *argp)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_ioctl_int(t, dh, request, argp);
}

static int tty_fcntl(fs_handle h, int cmd, int arg)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_fcntl_int(t, dh, cmd, arg);
}

static struct kcond *tty_get_rready_cond(fs_handle h)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return &t->input_cond;
}

static bool tty_read_ready(fs_handle h)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_read_ready_int(t, dh);
}

static int
tty_create_device_file(int minor,
                       const struct file_ops **fops_r,
                       enum vfs_entry_type *t)
{
   static const struct file_ops static_ops_tty = {

      .read = tty_read,
      .write = tty_write,
      .ioctl = tty_ioctl,
      .fcntl = tty_fcntl,
      .get_rready_cond = tty_get_rready_cond,
      .read_ready = tty_read_ready,

      /*
       * IMPORTANT: remember to add any NEW ops func also to ttyaux's
       * ttyaux_create_device_file() function, in ttyaux.c.
       */

      /* the tty device-file requires NO locking */
      .exlock = vfs_file_nolock,
      .exunlock = vfs_file_nolock,
      .shlock = vfs_file_nolock,
      .shunlock = vfs_file_nolock,
   };

   *t = VFS_CHAR_DEV;
   *fops_r = &static_ops_tty;
   return 0;
}

static void init_tty_struct(struct tty *t, u16 minor, u16 serial_port_fwd)
{
   t->minor = minor;
   t->filter_ctx.t = t;
   t->c_term = default_termios;
   t->kd_mode = KD_TEXT;
   t->curr_color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);
   t->user_color = t->curr_color;
   t->serial_port_fwd = serial_port_fwd;
   t->c_set = 0;
   t->c_sets_tables[0] = tty_default_trans_table;
   t->c_sets_tables[1] = tty_gfx_trans_table;
}

int tty_get_num(struct tty *t)
{
   return t->minor;
}

void
tty_create_devfile_or_panic(const char *filename, u16 major, u16 minor)
{
   int rc;

   if ((rc = create_dev_file(filename, major, minor)) < 0)
      panic("TTY: unable to create devfile /dev/%s (error: %d)", filename, rc);
}

static struct term *
tty_allocate_and_init_new_term(u16 serial_port_fwd, int rows_buf)
{
   struct term *new_term = alloc_term_struct();

   if (!new_term)
      panic("TTY: no enough memory a new term instance");

   if (init_term(new_term,
                 first_term_initial_vi,
                 term_get_rows(ttys[1]->term_inst),
                 term_get_cols(ttys[1]->term_inst),
                 serial_port_fwd,
                 rows_buf) < 0)
   {
      free_term_struct(new_term);
      return NULL;
   }

   return new_term;
}

static struct tty *
allocate_and_init_tty(u16 minor, u16 serial_port_fwd, int rows_buf)
{
   struct tty *t = kzmalloc(sizeof(struct tty));

   if (!t)
      return NULL;

   init_tty_struct(t, minor, serial_port_fwd);

   struct term *new_term =
      (minor == 1 || kopt_serial_console)
         ? get_curr_term()
         : tty_allocate_and_init_new_term(serial_port_fwd, rows_buf);

   if (!new_term) {
      kfree2(t, sizeof(struct tty));
      return NULL;
   }

   t->term_inst = new_term;
   tty_reset_filter_ctx(t);

   if (serial_port_fwd)
      term_set_filter(new_term, &serial_tty_write_filter, &t->filter_ctx);

   return t;
}

static void
tty_full_destroy(struct tty *t)
{
   if (t->term_inst) {
      dispose_term(t->term_inst);
      free_term_struct(t->term_inst);
   }

   kfree2(t, sizeof(struct tty));
}

struct tty *create_tty_nodev(void)
{
   struct tty *const t = allocate_and_init_tty(0, 0, 0);

   if (!t)
      return NULL;

   tty_input_init(t);
   tty_update_default_state_tables(t);
   return t;
}

static int internal_init_tty(u16 major, u16 minor, u16 serial_port_fwd)
{
   ASSERT(minor < ARRAY_SIZE(ttys));
   ASSERT(!ttys[minor]);

   struct tty *const t = allocate_and_init_tty(minor, serial_port_fwd, -1);

   if (!t)
      return -ENOMEM;

   snprintk(t->dev_filename,
            sizeof(t->dev_filename),
            serial_port_fwd ? "ttyS%d" : "tty%d",
            serial_port_fwd ? minor - TTYS0_MINOR : minor);

   if (create_dev_file(t->dev_filename, major, minor) < 0) {
      tty_full_destroy(t);
      return -ENOMEM;
   }

   tty_input_init(t);
   tty_update_default_state_tables(t);
   ttys[minor] = t;
   return 0;
}

static void init_video_ttys(void)
{
   for (u16 i = 1; i <= kopt_tty_count; i++) {
      if (internal_init_tty(TTY_MAJOR, i, 0) < 0) {

         if (i <= 1)
            panic("No enough memory for any TTY device");

         printk("WARNING: no enough memory for creating /dev/tty%d\n", i);
         kopt_tty_count = i - 1;
         break;
      }
   }
}

static void init_serial_ttys(void)
{
   /* NOTE: hw-specific stuff in generic code. TODO: fix that. */
   static const u16 com_ports[4] = {COM1, COM2, COM3, COM4};

   for (u16 i = 0; i < 4; i++) {
      if (internal_init_tty(TTY_MAJOR, i + TTYS0_MINOR, com_ports[i]) < 0) {
         printk("WARNING: no enough memory for creating /dev/ttyS%d\n", i);
         break;
      }
   }
}

void init_tty(void)
{
   first_term_initial_vi = term_get_vi(get_curr_term());
   struct driver_info *di = kzmalloc(sizeof(struct driver_info));

   if (!di)
      panic("TTY: no enough memory for struct driver_info");

   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   register_driver(di, TTY_MAJOR);

   /*
    * tty0 is special: not a real tty but a special file always pointing
    * to the current tty. Therefore, just create the dev file.
    */
   tty_create_devfile_or_panic("tty0", di->major, 0);

   if (!kopt_serial_console)
      init_video_ttys();

   init_serial_ttys();

   disable_preemption();
   {
      if (!kopt_serial_console)
         kb_register_keypress_handler(&tty_keypress_handler_elem);

      tty_tasklet_runner = create_tasklet_thread(100, TTY_TASKLETS_QUEUE_SIZE);

      if (tty_tasklet_runner < 0)
         panic("TTY: unable to create tasklet runner");
   }
   enable_preemption();

   init_ttyaux();
   __curr_tty = ttys[kopt_serial_console ? TTYS0_MINOR : 1];

   process_set_tty(kernel_process_pi, get_curr_tty());
}
