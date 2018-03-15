
#include <kmalloc.h>
#include <fs/devfs.h>
#include <string_util.h>
#include <term.h>
#include <exos_errno.h>

filesystem *devfs;

static ssize_t stub_read (fs_handle h, char *buf, size_t size)
{
   return 0;
}

static ssize_t stdout_write (fs_handle h, char *buf, size_t size)
{
   for (size_t i = 0; i < size; i++)
      term_write_char(buf[i]);

   return size;
}

static ssize_t tty_ioctl(fs_handle h, uptr request, void *argp);

fs_handle devfs_open(filesystem *fs, const char *path)
{
   devfs_file_handle *h;
   int id;

   /* Temporary hacky implementation */

   if (!strcmp(path, "/stdin")) {
      id = 1000;
   } else if (!strcmp(path, "/stdout")) {
      id = 1001;
   } else if (!strcmp(path, "/stderr")) {
      id = 1002;
   } else {
      return NULL;
   }

   h = kzmalloc(sizeof(devfs_file_handle));
   h->fs = fs;
   h->id = id;

   h->fops.fseek = NULL;
   h->fops.fread = stub_read;

   if (id == 1001 || id == 1002) {
      h->fops.fwrite = stdout_write;
      h->fops.ioctl = tty_ioctl;
   }

   return h;
}

void devfs_close(fs_handle h)
{
   devfs_file_handle *devh = h;
   kfree(devh, sizeof(devfs_file_handle));
}

filesystem *create_devfs(void)
{
   filesystem *fs = kzmalloc(sizeof(filesystem));

   fs->device_data = NULL; /* unused for now */
   fs->fopen = devfs_open;
   fs->fclose = devfs_close;

   return fs;
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
   devfs_file_handle *devh = h;

   if (devh->id == 1001 || devh->id == 1002) {

      if (request == TCGETS) {
         memmove(argp, &hard_coded_termios, sizeof(termios));
         return 0;
      }

   }

   return -EINVAL;
}
