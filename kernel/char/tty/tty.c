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

STATIC_ASSERT(TTY_COUNT <= MAX_TTYS);

tty *ttys[MAX_TTYS + 1];
tty *__curr_tty;
int tty_tasklet_runner;

static ssize_t tty_read(fs_handle h, char *buf, size_t size)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_read_int(t, dh, buf, size);
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_write_int(t, dh, buf, size);
}

static int tty_ioctl(fs_handle h, uptr request, void *argp)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_ioctl_int(t, dh, request, argp);
}

static int tty_fcntl(fs_handle h, int cmd, uptr arg)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return tty_fcntl_int(t, dh, cmd, arg);
}

static kcond *tty_get_rready_cond(fs_handle h)
{
   devfs_file_handle *dh = h;
   devfs_file *df = dh->devfs_file_ptr;
   tty *t = df->dev_minor ? ttys[df->dev_minor] : get_curr_tty();

   return &t->kb_input_cond;
}

static int
tty_create_device_file(int minor, file_ops *ops, devfs_entry_type *t)
{
   *t = DEVFS_CHAR_DEVICE;

   bzero(ops, sizeof(file_ops));

   ops->read = tty_read;
   ops->write = tty_write;
   ops->ioctl = tty_ioctl;
   ops->fcntl = tty_fcntl;
   ops->get_rready_cond = tty_get_rready_cond;

   /* the tty device-file requires NO locking */
   ops->exlock = &vfs_file_nolock;
   ops->exunlock = &vfs_file_nolock;
   ops->shlock = &vfs_file_nolock;
   ops->shunlock = &vfs_file_nolock;
   return 0;
}

static void init_tty_struct(tty *t, int minor)
{
   t->minor = minor;
   t->filter_ctx.t = t;
   t->c_term = default_termios;
   t->kd_mode = KD_TEXT;
   t->curr_color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);
}

int tty_get_num(tty *t)
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

static term *
tty_allocate_and_init_new_term(void)
{
   term *new_term = alloc_term_struct();

   if (!new_term)
      panic("TTY: no enough memory a new term instance");

   if (init_term(new_term,
                 term_get_vi(ttys[1]->term_inst),
                 term_get_rows(ttys[1]->term_inst),
                 term_get_cols(ttys[1]->term_inst)) < 0)
   {
      free_term_struct(new_term);
      return NULL;
   }

   return new_term;
}

static tty *allocate_and_init_tty(u16 minor)
{
   tty *t = kzmalloc(sizeof(tty));

   if (!t)
      return NULL;

   init_tty_struct(t, minor);

   term *new_term = (minor == 1)
                        ? get_curr_term()
                        : tty_allocate_and_init_new_term();

   if (!new_term) {
      kfree2(t, sizeof(tty));
      return NULL;
   }

   t->term_inst = new_term;
   return t;
}

static void
tty_full_destroy(tty *t)
{
   if (t->term_inst) {
      dispose_term(t->term_inst);
      free_term_struct(t->term_inst);
   }

   kfree2(t, sizeof(tty));
}

static int internal_init_tty(u16 major, u16 minor)
{
   ASSERT(minor < ARRAY_SIZE(ttys));
   ASSERT(!ttys[minor]);

   if (minor == 0) {

      /*
       * tty0 is special: not a real tty but a special file always pointing
       * to the current tty. Therefore, just create the dev file.
       */

      tty_create_devfile_or_panic("tty0", major, minor);
      return 0;
   }

   tty *const t = allocate_and_init_tty(minor);

   if (!t)
      return -ENOMEM;

   snprintk(t->dev_filename, sizeof(t->dev_filename), "tty%d", minor);

   if (create_dev_file(t->dev_filename, major, minor) < 0) {
      tty_full_destroy(t);
      return -ENOMEM;
   }

   tty_input_init(t);
   term_set_filter(t->term_inst, tty_term_write_filter, &t->filter_ctx);
   tty_update_default_state_tables(t);
   ttys[minor] = t;
   return 0;
}

void init_tty(void)
{
   driver_info *di = kzmalloc(sizeof(driver_info));

   if (!di)
      panic("TTY: no enough memory for driver_info");

   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   register_driver(di, TTY_MAJOR);

   for (u16 i = 0; i <= kopt_tty_count; i++) {
      if (internal_init_tty(TTY_MAJOR, i) < 0) {

         if (i <= 1)
            panic("No enough memory for any TTY device");

         printk("WARNING: no enough memory for creating /dev/tty%d\n", i);
         kopt_tty_count = i - 1;
         break;
      }
   }

   disable_preemption();
   {
      if (kb_register_keypress_handler(&tty_keypress_handler) < 0)
         panic("TTY: unable to register keypress handler");

      tty_tasklet_runner = create_tasklet_thread(100, 1024);

      if (tty_tasklet_runner < 0)
         panic("TTY: unable to create tasklet runner");
   }
   enable_preemption();

   init_ttyaux();
   __curr_tty = ttys[1];

   process_set_tty(kernel_process_pi, get_curr_tty());
   void *init_pi = task_get_pi_opaque(get_task(1));

   if (init_pi != NULL)
      process_set_tty(init_pi, get_curr_tty());
}
