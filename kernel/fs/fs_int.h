/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs.h>

struct mountpoint {

   REF_COUNTED_OBJECT;

   vfs_inode_ptr_t host_fs_inode;
   struct mnt_fs *host_fs;
   struct mnt_fs *target_fs;
};

#define RESOLVE_STACK_SIZE       4

struct vfs_resolve_int_ctx {

   int ss;                                       /* stack size */
   bool exlock;                                  /* true -> use exlock,
                                                    false -> use shlock */

   const char *orig_paths[RESOLVE_STACK_SIZE];   /* original paths stack */
   struct vfs_path paths[RESOLVE_STACK_SIZE];    /* vfs paths stack */
   char sym_paths[RESOLVE_STACK_SIZE][MAX_PATH]; /* symlinks paths stack */
};
