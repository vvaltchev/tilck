
#include <kmalloc.h>
#include <fs/devfs.h>
#include <string_util.h>
#include <term.h>
#include <exos_errno.h>
#include <list.h>

filesystem *devfs;

/*
 * Registered drivers. The major number is just an index in this array.
 */
static driver_info *drivers[16];
static u32 drivers_count;

/*
 * Registers the driver described by 'info'.
 * Returns driver's major number.
 */
int register_driver(driver_info *info)
{
   /* Be sure there's always enough space. */
   VERIFY(drivers_count < ARRAY_SIZE(drivers) - 1);

   drivers[drivers_count] = info;
   return drivers_count++;
}

typedef struct {

   list_node list;
   const char *name;
   file_ops fops;

} devfs_file;

typedef struct {

   list_node files_list;

} devfs_data;

int create_dev_file(const char *filename, int major, int minor)
{
   ASSERT(devfs != NULL);

   if (major < 0 || major >= (int)drivers_count)
      return -1;

   filesystem *fs = devfs;
   driver_info *dinfo = drivers[major];

   devfs_data *d = fs->device_data;
   devfs_file *f = kmalloc(sizeof(devfs_file));
   list_node_init(&f->list);
   f->name = filename;

   int res = dinfo->create_dev_file(minor, &f->fops);

   if (res < 0) {
      kfree2(f, sizeof(devfs_file));
      return res;
   }

   list_add_tail(&d->files_list, &f->list);
   return 0;
}

static fs_handle devfs_open(filesystem *fs, const char *path)
{
   /*
    * Path is expected to be striped from the mountpoint prefix, but the '/'
    * is kept. In other words, /dev/tty is /tty here.
    */

   ASSERT(*path == '/');
   path++;

   devfs_data *d = fs->device_data;
   devfs_file *pos;

   /* Trivial implementation for the moment: linearly iterate the linked list */

   list_for_each(pos, &d->files_list, list) {
      if (!strcmp(pos->name, path)) {

         devfs_file_handle *h;
         h = kzmalloc(sizeof(devfs_file_handle));
         h->fs = fs;
         h->fops = pos->fops;

         return h;
      }
   }

   return NULL;
}

static void devfs_close(fs_handle h)
{
   devfs_file_handle *devh = h;
   kfree2(devh, sizeof(devfs_file_handle));
}

static fs_handle devfs_dup(fs_handle h)
{
   devfs_file_handle *new_h;
   new_h = kzmalloc(sizeof(devfs_file_handle));
   VERIFY(new_h != NULL);
   memmove(new_h, h, sizeof(devfs_file_handle));
   return new_h;
}

filesystem *create_devfs(void)
{
   /* Disallow multiple instances of devfs */
   ASSERT(devfs == NULL);

   filesystem *fs = kzmalloc(sizeof(filesystem));
   devfs_data *d = kzmalloc(sizeof(devfs_data));

   list_node_init(&d->files_list);

   fs->device_data = d;
   fs->fopen = devfs_open;
   fs->fclose = devfs_close;
   fs->dup = devfs_dup;

   return fs;
}

void create_and_register_devfs(void)
{
   devfs = create_devfs();
   mountpoint_add(devfs, "/dev/");
}
