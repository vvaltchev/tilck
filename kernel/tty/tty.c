/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_console.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/test/tty_test.h>

#include <tilck/mods/console.h>

#include <linux/major.h> // system header

#include "tty_int.h"

#define TTYS0_MINOR 64

STATIC_ASSERT(TTY_COUNT <= MAX_TTYS);

struct tty *ttys[128];
struct tty *__curr_tty;
static struct term_params first_term_i;

static struct keypress_handler_elem tty_keypress_handler_elem =
{
   .handler = &tty_keypress_handler
};

STATIC_ASSERT(ARRAY_SIZE(ttys) > MAX_TTYS);

static ssize_t tty_read(fs_handle h, char *buf, size_t size, offt *pos)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   ASSERT(*pos == 0);
   return tty_read_int(t, dh, buf, size);
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size, offt *pos)
{
   struct devfs_handle *dh = h;
   struct devfs_file *df = dh->file;
   struct tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   ASSERT(*pos == 0);
   return tty_write_int(t, dh, buf, size);
}

static int tty_ioctl(fs_handle h, ulong request, void *argp)
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

int
tty_create_extra(int minor, void *extra)
{
   struct tty_handle_extra *eh = extra;
   eh->read_buf = kzmalloc(TTY_READ_BS);

   if (!eh->read_buf)
      return -ENOMEM;

   return 0;
}

int
tty_on_dup_extra(int minor, void *extra)
{
   struct tty_handle_extra *eh = extra;
   char *new_buf;
   ASSERT(eh->read_buf);

   if (!(new_buf = kmalloc(TTY_READ_BS)))
      return -ENOMEM;

   memcpy32(new_buf, eh->read_buf, TTY_READ_BS / 4);
   eh->read_buf = new_buf;
   return 0;
}

void
tty_destroy_extra(int minor, void *extra)
{
   struct tty_handle_extra *eh = extra;
   ASSERT(eh->read_buf);
   kfree2(eh->read_buf, TTY_READ_BS);
}

static int
tty_create_device_file(int minor,
                       enum vfs_entry_type *type,
                       struct devfs_file_info *nfo)
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

   *type = VFS_CHAR_DEV;
   nfo->fops = &static_ops_tty;
   nfo->create_extra = &tty_create_extra;
   nfo->destroy_extra = &tty_destroy_extra;
   nfo->on_dup_extra = &tty_on_dup_extra;
   return 0;
}

static void init_tty_struct(struct tty *t, u16 minor, u16 serial_port_fwd)
{
   t->minor = minor;
   t->serial_port_fwd = serial_port_fwd;
   t->kd_gfx_mode = KD_TEXT;
   t->curr_color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);
   tty_reset_termios(t);

   if (MOD_console_actual && !serial_port_fwd)
      reset_console_data(t);
}

int tty_get_num(struct tty *t)
{
   return t->minor;
}

void
tty_create_devfile_or_panic(const char *filename,
                            u16 major,
                            u16 minor,
                            void **devfile)
{
   int rc;

   if ((rc = create_dev_file(filename, major, minor, devfile)) < 0)
      panic("TTY: unable to create devfile /dev/%s (error: %d)", filename, rc);
}

static term *
tty_allocate_and_init_new_video_term(int rows_buf)
{
   const struct term_interface *intf = video_term_intf;
   term *new_term = intf->alloc();

   if (!new_term)
      panic("TTY: no enough memory a new term instance");

   if (intf->video_term_init(new_term,
                             first_term_i.vi,
                             first_term_i.rows,
                             first_term_i.cols,
                             rows_buf) < 0)
   {
      intf->free(new_term);
      return NULL;
   }

   return new_term;
}

static term *
tty_allocate_and_init_new_serial_term(u16 serial_port_fwd)
{
   const struct term_interface *intf = serial_term_intf;
   term *new_term = serial_term_intf->alloc();

   if (!new_term)
      panic("TTY: no enough memory a new term instance");

   if (intf->serial_term_init(new_term, serial_port_fwd) < 0)
   {
      intf->free(new_term);
      return NULL;
   }

   return new_term;
}

static void
tty_full_destroy(struct tty *t)
{
   if (t->tstate) {
      t->tintf->dispose(t->tstate);
      t->tintf->free(t->tstate);
   }

   if (MOD_console_actual) {
      free_console_data(t->console_data);
   }

   kfree_array_obj(t->ctrl_handlers, tty_ctrl_sig_func, 256);
   kfree_obj(t->input_buf, TTY_INPUT_BS);
   kfree_obj(t, struct tty);
}


STATIC struct tty *
allocate_and_init_tty(u16 minor, u16 serial_port_fwd, int rows_buf)
{
   struct tty *t;
   term *new_term = get_curr_term();
   const struct term_interface *new_term_intf;

   if (!(t = kzalloc_obj(struct tty)))
      return NULL;

   if (!(t->input_buf = kzmalloc(TTY_INPUT_BS))) {
      tty_full_destroy(t);
      return NULL;
   }

   if (!(t->ctrl_handlers = kzalloc_array_obj(tty_ctrl_sig_func, 256))) {
      tty_full_destroy(t);
      return NULL;
   }

   if (MOD_console_actual && !serial_port_fwd) {
      if (!(t->console_data = alloc_console_data())) {
         tty_full_destroy(t);
         return NULL;
      }
   }

   init_tty_struct(t, minor, serial_port_fwd);
   new_term_intf = serial_port_fwd ? serial_term_intf : video_term_intf;

   if (minor != 1 && !kopt_sercon) {

      new_term =
         serial_port_fwd
            ? tty_allocate_and_init_new_serial_term(serial_port_fwd)
            : tty_allocate_and_init_new_video_term(rows_buf);
   }

   if (!new_term) {
      kfree_obj(t, struct tty);
      return NULL;
   }

   t->tstate = new_term;
   t->tintf = new_term_intf;
   t->tintf->get_params(t->tstate, &t->tparams);
   tty_reset_filter_ctx(t);
   return t;
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

   if (create_dev_file(t->dev_filename, major, minor, &t->devfile) < 0) {
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
   for (u16 i = 1; i <= kopt_ttys; i++) {
      if (internal_init_tty(TTY_MAJOR, i, 0) < 0) {

         if (i <= 1)
            panic("No enough memory for any TTY device");

         printk("WARNING: no enough memory for creating /dev/tty%d\n", i);
         kopt_ttys = i - 1;
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

ssize_t
tty_write_int(struct tty *t,
              struct devfs_handle *h,
              const char *buf,
              size_t size)
{
   size = MIN(size, MAX_TERM_WRITE_LEN);
   t->tintf->write(t->tstate, buf, size, t->curr_color);
   return (ssize_t) size;
}

ssize_t tty_curr_proc_write(const char *buf, size_t size)
{
   return tty_write_int(get_curr_process_tty(), NULL, buf, size);
}

void tty_write_on_all_ttys(const char *buf, size_t size)
{
   for (u32 i = 0; i < ARRAY_SIZE(ttys); i++) {

      if (ttys[i])
         tty_write_int(ttys[i], NULL, buf, size);
   }
}

STATIC void init_tty(void)
{
   process_term_read_info(&first_term_i);
   struct driver_info *di = kzalloc_obj(struct driver_info);

   if (!di)
      panic("TTY: no enough memory for struct driver_info");

   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   register_driver(di, TTY_MAJOR);

   /*
    * tty0 is special: not a real tty but a special file always pointing
    * to the current tty. Therefore, just create the dev file.
    */
   tty_create_devfile_or_panic("tty0", di->major, 0, NULL);

   if (!kopt_sercon)
      if (video_term_intf)
         init_video_ttys();

   if (serial_term_intf)
      init_serial_ttys();

   /* Make init's process group to be the fg process group for the first tty */
   ttys[kopt_sercon ? TTYS0_MINOR : 1]->fg_pgid = 1;

   disable_preemption();
   {
      if (!kopt_sercon)
         register_keypress_handler(&tty_keypress_handler_elem);
   }
   enable_preemption();

   init_ttyaux();
   __curr_tty = ttys[kopt_sercon ? TTYS0_MINOR : 1];

   process_set_tty(kernel_process_pi, get_curr_tty());
}

static struct module tty_module = {

   .name = "tty",
   .priority = MOD_tty_prio,
   .init = &init_tty,
};

REGISTER_MODULE(&tty_module);

/* No console funcs */

#if !MOD_console && !defined(KERNEL_TEST)

void tty_update_default_state_tables(struct tty *t)
{
   /* do nothing */
}

void tty_reset_filter_ctx(struct tty *t)
{
   /* do nothing */
}

#endif
