/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/list.h>
#include <tilck/kernel/fs/vfs_base.h>
#include <tilck/kernel/subsystems.h>

struct locked_file; /* forward declaration */

int
acquire_subsystem_file_exlock(struct fs *fs,
                              vfs_inode_ptr_t i,
                              enum subsystem subsys,
                              struct locked_file **lock_ref);   /* out */

int
acquire_subsystem_file_exlock_h(fs_handle h,
                                enum subsystem subsys,
                                struct locked_file **lock_ref); /* out */

void retain_subsystem_file_exlock(struct locked_file *lf);
void release_subsystem_file_exlock(struct locked_file *lf);
