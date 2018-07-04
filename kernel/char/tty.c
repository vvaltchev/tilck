
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

extern struct termios curr_termios;

#define KB_CBUF_SIZE 256

static char kb_cooked_buf[256];
static ringbuf kb_cooked_ringbuf;
static kcond kb_cond;

static inline bool kb_cbuf_is_empty(void)
{
   return ringbuf_is_empty(&kb_cooked_ringbuf);
}

static inline bool kb_cbuf_is_full(void)
{
   return ringbuf_is_full(&kb_cooked_ringbuf);
}

static inline char kb_cbuf_read_elem(void)
{
   u8 ret;
   ASSERT(!kb_cbuf_is_empty());
   DEBUG_CHECKED_SUCCESS(ringbuf_read_elem1(&kb_cooked_ringbuf, &ret));
   return (char)ret;
}

static inline bool kb_cbuf_drop_last_written_elem(void)
{
   char unused;
   return ringbuf_unwrite_elem(&kb_cooked_ringbuf, &unused);
}

static inline bool kb_cbuf_write_elem(char c)
{
   return ringbuf_write_elem1(&kb_cooked_ringbuf, c);
}

static int tty_keypress_handler(u32 key, u8 c)
{
   if (key == KEY_E0_PAGE_UP) {
      term_scroll_up(5);
      return KB_HANDLER_OK_AND_STOP;
   }

   if (key == KEY_E0_PAGE_DOWN) {
      term_scroll_down(5);
      return KB_HANDLER_OK_AND_STOP;
   }

   if (!c)
      return KB_HANDLER_NAK;

   if (c == '\b') {

      if (kb_cbuf_drop_last_written_elem())
         term_write((char *)&c, 1);

      return KB_HANDLER_OK_AND_CONTINUE;
   }

   if (kb_cbuf_write_elem(c)) {

      term_write((char *)&c, 1);

      if (c == '\n' || kb_cbuf_is_full()) {
         kcond_signal_one(&kb_cond);
      }
   }

   return KB_HANDLER_OK_AND_CONTINUE;
}

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
   driver_info *di = kmalloc(sizeof(driver_info));
   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   int major = register_driver(di);
   int rc = create_dev_file("tty", major, 0 /* minor */);

   if (rc != 0)
      panic("TTY: unable to create /dev/tty (error: %d)", rc);

   kcond_init(&kb_cond);
   ringbuf_init(&kb_cooked_ringbuf, KB_CBUF_SIZE, 1, kb_cooked_buf);

   if (kb_register_keypress_handler(&tty_keypress_handler) < 0)
      panic("TTY: unable to register keypress handler");
}
