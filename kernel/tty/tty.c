/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/modules.h>
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
static struct tilck_term_info first_term_i;

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

static struct kcond *tty_get_rready_cond(fs_handle h)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return &t->input_cond;
}

static int tty_read_ready(fs_handle h)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_read_ready_int(t, dh);
}

static int
tty_create_device_file(int minor,
                       const struct file_ops **fops_r,
                       enum vfs_entry_type *t,
                       int *spec_flags_ref)
{
   static const struct file_ops static_ops_tty = {

      .read = tty_read,
      .write = tty_write,
      .ioctl = tty_ioctl,
      .get_rready_cond = tty_get_rready_cond,
      .read_ready = tty_read_ready,

      /*
       * IMPORTANT: remember to add any NEW ops func also to ttyaux's
       * ttyaux_create_device_file() function, in ttyaux.c.
       */
   };

   *t = VFS_CHAR_DEV;
   *fops_r = &static_ops_tty;
   return 0;
}

static void init_tty_struct(struct tty *t, u16 minor, u16 serial_port_fwd)
{
   t->minor = minor;
   t->serial_port_fwd = serial_port_fwd;
   t->c_term = default_termios;
   t->kd_gfx_mode = KD_TEXT;
   t->curr_color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);

#if MOD_console
   t->user_color = t->curr_color;
   t->filter_ctx.t = t;
   t->c_set = 0;
   t->c_sets_tables[0] = tty_default_trans_table;
   t->c_sets_tables[1] = tty_gfx_trans_table;
#endif

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
                 first_term_i.vi,
                 first_term_i.rows,
                 first_term_i.cols,
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
   struct tty *t;

   if (!(t = kzmalloc(sizeof(struct tty))))
      return NULL;

   if (!(t->input_buf = kzmalloc(TTY_INPUT_BS))) {
      kfree2(t, sizeof(struct tty));
      return NULL;
   }

   if (!(t->special_ctrl_handlers = kzmalloc(256*sizeof(tty_ctrl_sig_func)))) {
      kfree2(t->input_buf, sizeof(TTY_INPUT_BS));
      kfree2(t, sizeof(struct tty));
      return NULL;
   }

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
   term_read_info(t->term_inst, &t->term_i);
   tty_reset_filter_ctx(t);

   if (serial_port_fwd)
      term_set_filter(new_term, NULL, NULL);

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

struct tty *get_serial_tty(int n)
{
   ASSERT(IN_RANGE(n, 0, 4));
   return ttys[TTYS0_MINOR + n];
}

static void init_tty(void)
{
   term_read_info(get_curr_term(), &first_term_i);
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
         register_keypress_handler(&tty_keypress_handler_elem);

      tty_tasklet_runner = create_tasklet_thread(100, TTY_TASKLETS_QUEUE_SIZE);

      if (tty_tasklet_runner < 0)
         panic("TTY: unable to create tasklet runner");
   }
   enable_preemption();

   init_ttyaux();
   __curr_tty = ttys[kopt_serial_console ? TTYS0_MINOR : 1];

   process_set_tty(kernel_process_pi, get_curr_tty());
}

static struct module tty_module = {

   .name = "tty",
   .priority = 200,
   .init = &init_tty,
};

REGISTER_MODULE(&tty_module);

/* No console funcs */

#if !MOD_console

void tty_update_default_state_tables(struct tty *t)
{
   /* do nothing */
}

ssize_t
tty_write_int(struct tty *t, struct devfs_handle *h, char *buf, size_t size)
{
   /* term_write's size is limited to 2^20 - 1 */
   size = MIN(size, (size_t)MB - 1);
   term_write(t->term_inst, buf, size, t->curr_color);
   return (ssize_t) size;
}

void tty_reset_filter_ctx(struct tty *t)
{
   /* do nothing */
}

#endif
