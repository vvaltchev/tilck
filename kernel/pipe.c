/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/pipe.h>

struct pipe {

   REF_COUNTED_OBJECT;

};

static ssize_t pipe_read(fs_handle h, char *buf, size_t size)
{
   return 0;
}

static ssize_t pipe_write(fs_handle h, char *buf, size_t size)
{
   return 0;
}

static const struct file_ops static_ops_pipe_read_end =
{
   .read = pipe_read,
};

static const struct file_ops static_ops_pipe_write_end =
{
   .write = pipe_write,
};


void unused_foo(void) {
   (void)static_ops_pipe_read_end;
   (void)static_ops_pipe_write_end;
}
