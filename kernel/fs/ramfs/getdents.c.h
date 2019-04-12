/* SPDX-License-Identifier: BSD-2-Clause */

static int
ramfs_getdents64_int(ramfs_handle *rh,
                     struct linux_dirent64 *dirp,
                     u32 buf_size)
{
   ramfs_inode *inode = rh->inode;
   u32 offset = 0, curr_index = 0;
   struct linux_dirent64 ent;
   ramfs_entry *pos;

   list_for_each_ro(pos, &inode->entries_list, node) {

      if (curr_index < rh->read_pos) {
         curr_index++;
         continue;
      }

      const char *const file_name = pos->name;
      const u32 fl = (u32)strlen(file_name);
      const u32 entry_size = fl + 1 + sizeof(struct linux_dirent64);

      if (offset + entry_size > buf_size) {

         if (!offset) {

            /*
            * We haven't "returned" any entries yet and the buffer is too small
            * for our first entry.
            */

            return -EINVAL;
         }

         /* We "returned" at least one entry */
         return (int)offset;
      }

      ent.d_ino = (typeof(ent.d_ino)) pos->inode->inode;
      ent.d_off = (s64)(offset + entry_size);
      ent.d_reclen = (u16)entry_size;

      switch (pos->inode->type) {
         case RAMFS_DIRECTORY:
            ent.d_type = DT_DIR;
            break;
         case RAMFS_FILE:
            ent.d_type = DT_REG;
            break;
         case RAMFS_SYMLINK:
            ent.d_type = DT_LNK;
            break;
         default:
            ent.d_type = DT_UNKNOWN;
            break;
      }

      struct linux_dirent64 *user_ent = (void *)((char *)dirp + offset);

      if (copy_to_user(user_ent, &ent, sizeof(ent)) < 0)
         return -EFAULT;

      if (copy_to_user(user_ent->d_name, file_name, fl + 1) < 0)
         return -EFAULT;

      offset = (u32) ent.d_off; /* s64 to u32 precision drop */
      curr_index++;
      rh->read_pos++;
   }

   return (int)offset;
}

static int
ramfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   int rc;
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;

   if (inode->type != RAMFS_DIRECTORY)
      return -ENOTDIR;

   rwlock_wp_shlock(&inode->rwlock);
   {
      rc = ramfs_getdents64_int(rh, dirp, buf_size);
   }
   rwlock_wp_shunlock(&inode->rwlock);
   return rc;
}
