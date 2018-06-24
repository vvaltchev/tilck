
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h> // for enable/disable preemption
#include <exos/kmalloc.h>
#include <exos/fs/devfs.h>
#include <exos/errno.h>
#include <exos/list.h>

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

   kmutex ex_mutex; // big exclusive whole-filesystem lock
                    // TODO: use a rw-lock when available in the kernel

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

static int devfs_open(filesystem *fs, const char *path, fs_handle *out)
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

         if (!h)
            return -ENOMEM;

         h->fs = fs;
         h->fops = pos->fops;
         *out = h;
         return 0;
      }
   }

   return -ENOENT;
}

static void devfs_close(fs_handle h)
{
   devfs_file_handle *devh = h;
   kfree2(devh, sizeof(devfs_file_handle));
}

static int devfs_dup(fs_handle h, fs_handle *dup_h)
{
   devfs_file_handle *new_h;
   new_h = kmalloc(sizeof(devfs_file_handle));

   if (!new_h)
      return -ENOMEM;

   memcpy(new_h, h, sizeof(devfs_file_handle));
   *dup_h = new_h;
   return 0;
}

static void devfs_exclusive_lock(filesystem *fs)
{
   devfs_data *d = fs->device_data;
   kmutex_lock(&d->ex_mutex);
}

static void devfs_exclusive_unlock(filesystem *fs)
{
   devfs_data *d = fs->device_data;
   kmutex_unlock(&d->ex_mutex);
}

static void devfs_shared_lock(filesystem *fs)
{
   devfs_exclusive_lock(fs);
}

static void devfs_shared_unlock(filesystem *fs)
{
   devfs_exclusive_unlock(fs);
}

filesystem *create_devfs(void)
{
   /* Disallow multiple instances of devfs */
   ASSERT(devfs == NULL);

   filesystem *fs = kzmalloc(sizeof(filesystem));

   if (!fs)
      return NULL;

   devfs_data *d = kzmalloc(sizeof(devfs_data));

   if (!d) {
      kfree2(fs, sizeof(filesystem));
      return NULL;
   }

   list_node_init(&d->files_list);
   kmutex_init(&d->ex_mutex, KMUTEX_FL_RECURSIVE);

   fs->flags = EXVFS_FS_RW;
   fs->device_id = exvfs_get_new_device_id();
   fs->device_data = d;
   fs->open = devfs_open;
   fs->close = devfs_close;
   fs->dup = devfs_dup;

   fs->fs_exlock = devfs_exclusive_lock;
   fs->fs_exunlock = devfs_exclusive_unlock;
   fs->fs_shlock = devfs_shared_lock;
   fs->fs_shunlock = devfs_shared_unlock;

   return fs;
}

void create_and_register_devfs(void)
{
   devfs = create_devfs();

   if (!devfs)
      panic("Unable to create devfs");

   int rc = mountpoint_add(devfs, "/dev/");

   if (rc != 0)
      panic("mountpoint_add() failed with error: %d", rc);
}
