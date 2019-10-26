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

   ATOMIC(int) read_handles;
   ATOMIC(int) write_handles;
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
   ssize_t rc = 0;

   kmutex_lock(&p->mutex);
   {
      if (p->read_handles == 0) {

         /*
          * Broken pipe.
          *
          * NOTE: in theory, we should send SIGPIPE to the current process and
          * return -EPIPE only if the signal is ignored, but since Tilck does
          * not support signal delivery yet, maybe it's better for now to return
          * always -EPIPE?
          *
          * TODO: think about the broken pipe behavior.
          */
         rc = -EPIPE;
         goto end;
      }

   again:

      /*
       * NOTE: this is a proof-of-concept implementation, writing byte by byte.
       * TODO: implement pipe_write() in a more efficient way.
       */
      while ((size_t)rc < size && ringbuf_write_elem1(&p->rb, (u8) buf[rc]))
         rc++;

      if (rc == 0) {
         kcond_wait(&p->cond, &p->mutex, KCOND_WAIT_FOREVER);
         goto again;
      }

      kcond_signal_one(&p->cond);

   end:;
   }
   kmutex_unlock(&p->mutex);
   return rc;
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
   // printk("Destroy pipe at %p\n", p);

   kcond_destory(&p->cond);
   kmutex_destroy(&p->mutex);
   ringbuf_destory(&p->rb);
   kfree2(p->buf, PIPE_BUF_SIZE);
   kfree2(p, sizeof(struct pipe));
}

static void on_pipe_handle_close(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;

   if (kh->fl_flags & O_RDONLY)
      p->read_handles--;

   if (kh->fl_flags & O_WRONLY)
      p->write_handles--;
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

   // printk("Create pipe at %p\n", p);

   p->on_handle_close = &on_pipe_handle_close;
   p->destory_obj = (void *)&destroy_pipe;
   ringbuf_init(&p->rb, PIPE_BUF_SIZE, 1, p->buf);
   kmutex_init(&p->mutex, 0);
   kcond_init(&p->cond);
   return p;
}

fs_handle pipe_create_read_handle(struct pipe *p)
{
   fs_handle res = NULL;

   res = kfs_create_new_handle(&static_ops_pipe_read_end, (void *)p, O_RDONLY);

   if (res != NULL)
      p->read_handles++;

   return res;
}

fs_handle pipe_create_write_handle(struct pipe *p)
{
   fs_handle res = NULL;

   res = kfs_create_new_handle(&static_ops_pipe_write_end, (void*)p, O_WRONLY);

   if (res != NULL)
      p->write_handles++;

   return res;
}
