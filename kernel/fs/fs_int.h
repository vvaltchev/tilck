/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs.h>

typedef struct {

   REF_COUNTED_OBJECT;

   struct fs *host_fs;
   vfs_inode_ptr_t host_fs_inode;
   struct fs *target_fs;

} mountpoint2;

#define RESOLVE_STACK_SIZE       4

typedef struct {

   int ss;                                       /* stack size */
   bool exlock;                                  /* true -> use exlock,
                                                    false -> use shlock */

   const char *orig_paths[RESOLVE_STACK_SIZE];   /* original paths stack */
   struct vfs_path paths[RESOLVE_STACK_SIZE];           /* vfs paths stack */
   char sym_paths[RESOLVE_STACK_SIZE][MAX_PATH]; /* symlinks paths stack */

} vfs_resolve_int_ctx;
