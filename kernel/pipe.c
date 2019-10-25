/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/pipe.h>
#include <tilck/kernel/fs/kernelfs.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/sync.h>

struct pipe {

   KOBJ_BASE_FIELDS

   char *buf;
   struct ringbuf rb;
   struct kmutex mutex;
   struct kcond cond;
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

static void destory_pipe(struct kobj_base *obj)
{
   struct pipe *p = (void *)obj;

   kcond_destory(&p->cond);
   kmutex_destroy(&p->mutex);
   ringbuf_destory(&p->rb);
   kfree2(p->buf, PIPE_BUF_SIZE);
   kfree2(p, sizeof(struct pipe));
}

struct pipe *create_pipe(void)
{
   struct pipe *p;

   if (!(p = (void *)kzmalloc(sizeof(struct pipe))))
      return NULL;

   if (!(p->buf = kmalloc(PIPE_BUF_SIZE))) {
      kfree2(p, sizeof(struct pipe));
      return NULL;
   }

   p->destory = &destory_pipe;
   ringbuf_init(&p->rb, PIPE_BUF_SIZE, 1, p->buf);
   kmutex_init(&p->mutex, 0);
   kcond_init(&p->cond);
   return p;
}

fs_handle pipe_create_read_handle(struct pipe *p)
{
   struct kernel_fs_handle *h = kfs_create_new_handle();
   h->fops = &static_ops_pipe_read_end;
   h->kobj = (void *)p;
   retain_obj(p);
   return h;
}

fs_handle pipe_create_write_handle(struct pipe *p)
{
   struct kernel_fs_handle *h = kfs_create_new_handle();
   h->fops = &static_ops_pipe_write_end;
   h->kobj = (void *)p;
   retain_obj(p);
   return h;
}
