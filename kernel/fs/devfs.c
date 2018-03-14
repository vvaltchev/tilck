
#include <kmalloc.h>
#include <fs/devfs.h>
#include <string_util.h>
#include <term.h>

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

   h = kmalloc(sizeof(devfs_file_handle));
   h->fs = fs;
   h->id = id;

   h->fops.fseek = NULL;
   h->fops.fread = stub_read;

   if (id == 1001 || id == 1002)
      h->fops.fwrite = stdout_write;
   else
      h->fops.fwrite = NULL;

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
