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
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   size_t rc = 0;

   kmutex_lock(&p->mutex);
   {
   again:

      /*
       * NOTE: this is a proof-of-concept implementation, reading byte by byte.
       * TODO: implement pipe_read() in a more efficient way.
       */
      while (rc < size && ringbuf_read_elem1(&p->rb, (u8 *)buf + rc))
         rc++;

      if (rc == 0) {
         kcond_wait(&p->cond, &p->mutex, KCOND_WAIT_FOREVER);
         goto again;
      }

      kcond_signal_one(&p->cond);
   }
   kmutex_unlock(&p->mutex);
   return (ssize_t)rc;
}

static ssize_t pipe_write(fs_handle h, char *buf, size_t size)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   size_t rc = 0;

   kmutex_lock(&p->mutex);
   {
   again:

      /*
       * NOTE: this is a proof-of-concept implementation, writing byte by byte.
       * TODO: implement pipe_write() in a more efficient way.
       */
      while (rc < size && ringbuf_write_elem1(&p->rb, (u8) buf[rc]))
         rc++;

      if (rc == 0) {
         kcond_wait(&p->cond, &p->mutex, KCOND_WAIT_FOREVER);
         goto again;
      }

      kcond_signal_one(&p->cond);
   }
   kmutex_unlock(&p->mutex);
   return (ssize_t)rc;
}

static const struct file_ops static_ops_pipe_read_end =
{
   .read = pipe_read,
};

static const struct file_ops static_ops_pipe_write_end =
{
   .write = pipe_write,
};

void destroy_pipe(struct pipe *p)
{
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

   p->destory = (void *)&destroy_pipe;
   ringbuf_init(&p->rb, PIPE_BUF_SIZE, 1, p->buf);
   kmutex_init(&p->mutex, 0);
   kcond_init(&p->cond);
   return p;
}

fs_handle pipe_create_read_handle(struct pipe *p)
{
   struct kfs_handle *h;

   if (!(h = kfs_create_new_handle()))
      return NULL;

   h->fops = &static_ops_pipe_read_end;
   h->kobj = (void *)p;
   h->fl_flags = O_RDONLY;
   retain_obj(p);
   return h;
}

fs_handle pipe_create_write_handle(struct pipe *p)
{
   struct kfs_handle *h;

   if (!(h = kfs_create_new_handle()))
      return NULL;

   h->fops = &static_ops_pipe_write_end;
   h->kobj = (void *)p;
   h->fl_flags = O_WRONLY;
   retain_obj(p);
   return h;
}
