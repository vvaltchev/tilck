
#include <fs/exvfs.h>
#include <fs/devfs.h>
#include <term.h>
#include <exos_errno.h>
#include <string_util.h>
#include <kmalloc.h>
#include <sync.h>
#include <kb.h>


static ssize_t tty_read(fs_handle h, char *buf, size_t size)
{
   enable_preemption();

   while (kb_cbuf_is_empty()) {
      kcond_wait(&kb_cond, NULL);
   }

   size_t i = 0;

   while (i < size && !kb_cbuf_is_empty()) {
      buf[i++] = kb_cbuf_read_elem();
   }

   disable_preemption();
   return i;
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   for (size_t i = 0; i < size; i++)
      term_write_char(buf[i]);

   return size;
}

/* -------------- TTY ioctl ------------- */


#define TCGETS 0x00005401

typedef unsigned char   cc_t;
typedef unsigned int    speed_t;
typedef unsigned int    tcflag_t;


#define NCCS 19
typedef struct {
   tcflag_t c_iflag;           /* input mode flags */
   tcflag_t c_oflag;           /* output mode flags */
   tcflag_t c_cflag;           /* control mode flags */
   tcflag_t c_lflag;           /* local mode flags */
   cc_t c_line;                /* line discipline */
   cc_t c_cc[NCCS];            /* control characters */
} termios;

static const termios hard_coded_termios =
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
};

static ssize_t tty_ioctl(fs_handle h, uptr request, void *argp)
{

   if (request == TCGETS) {
      memmove(argp, &hard_coded_termios, sizeof(termios));
      return 0;
   }

   return -EINVAL;
}

/* ----------------- Driver interface ---------------- */

static int tty_create_device_file(int minor, file_ops *ops)
{
   (void)minor; /* ignored */

   ops->fread = tty_read;
   ops->fwrite = tty_write;
   ops->fseek = NULL;
   ops->ioctl = tty_ioctl;

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
