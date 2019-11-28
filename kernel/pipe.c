/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/pipe.h>
#include <tilck/kernel/fs/kernelfs.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/signal.h>

struct pipe {

   KOBJ_BASE_FIELDS

   char *buf;
   struct ringbuf rb;
   struct kmutex mutex;
   struct kcond rcond;
   struct kcond wcond;
   struct kcond errcond;

   bool read_must_block;

   ATOMIC(int) read_handles;
   ATOMIC(int) write_handles;
};

static ssize_t pipe_read(fs_handle h, char *buf, size_t size)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool was_buffer_full;
   ssize_t rc = 0;

   if (!size)
      return 0;

   kmutex_lock(&p->mutex);
   {
   again:
      was_buffer_full = ringbuf_is_full(&p->rb);

      if (!(rc = (ssize_t)ringbuf_read_bytes(&p->rb, (u8 *)buf, size))) {

         if (atomic_load_explicit(&p->write_handles, mo_relaxed) == 0) {
            /* No more writers, always return 0, no matter what. */
            goto end;
         }

         if (p->read_must_block) {

            if (kh->fl_flags & O_NONBLOCK) {
               rc = -EAGAIN;
               goto end;
            }

            kcond_wait(&p->wcond, &p->mutex, KCOND_WAIT_FOREVER);
            goto again;

         } else {

            p->read_must_block = true;
            goto end;
         }
      }

      if (!ringbuf_is_empty(&p->rb)) {
         /* Notify other readers that's possible to read from the pipe */
         kcond_signal_all(&p->rcond);
      }

      if (was_buffer_full) {
         /* Notify all writers that now is possible to write to the pipe */
         kcond_signal_all(&p->wcond);
      }

   end:;
   }
   kmutex_unlock(&p->mutex);
   return rc;
}

static ssize_t pipe_write(fs_handle h, char *buf, size_t size)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   ssize_t rc = 0;
   bool was_buffer_empty;

   if (!size)
      return 0;

   kmutex_lock(&p->mutex);
   {
   again:
      was_buffer_empty = ringbuf_is_empty(&p->rb);

      if (atomic_load_explicit(&p->read_handles, mo_relaxed) == 0) {

         /* Broken pipe */

         int pid = get_curr_pid();
         send_signal(pid, SIGPIPE, true);

         rc = -EPIPE;
         goto end;
      }

      if (!(rc = (ssize_t)ringbuf_write_bytes(&p->rb, (u8 *)buf, size))) {

         if (kh->fl_flags & O_NONBLOCK) {
            rc = -EAGAIN;
            goto end;
         }

         kcond_wait(&p->rcond, &p->mutex, KCOND_WAIT_FOREVER);
         goto again;
      }

      p->read_must_block = false;

      if (!ringbuf_is_full(&p->rb)) {
         /* Notify other writers that now it possible to write on the pipe */
         kcond_signal_all(&p->wcond);
      }

      if (was_buffer_empty) {
         /* Notify all readers that now is possible to read from the pipe */
         kcond_signal_all(&p->rcond);
      }

   end:;
   }
   kmutex_unlock(&p->mutex);
   return rc;
}

static int pipe_read_ready(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool ret;

   kmutex_lock(&p->mutex);
   {
      ret = !ringbuf_is_empty(&p->rb) ||
            !p->read_must_block       ||
            atomic_load_explicit(&p->write_handles, mo_relaxed) == 0;
   }
   kmutex_unlock(&p->mutex);
   return ret;
}

static struct kcond *pipe_get_rready_cond(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   return &p->rcond;
}

static int pipe_write_ready(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool ret;

   kmutex_lock(&p->mutex);
   {
      ret = !ringbuf_is_full(&p->rb) ||
            atomic_load_explicit(&p->read_handles, mo_relaxed) == 0;
   }
   kmutex_unlock(&p->mutex);
   return ret;
}

static int pipe_except_ready(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   int ret = 0;

   kmutex_lock(&p->mutex);
   {
      if (atomic_load_explicit(&p->read_handles, mo_relaxed) == 0)
         ret |= POLLERR;

      if (atomic_load_explicit(&p->write_handles, mo_relaxed) == 0)
         ret |= POLLHUP;
   }
   kmutex_unlock(&p->mutex);
   return ret;
}

static struct kcond *pipe_get_wready_cond(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   return &p->wcond;
}

static struct kcond *pipe_get_except_cond(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   return &p->errcond;
}

static const struct file_ops static_ops_pipe_read_end =
{
   .read = pipe_read,
   .read_ready = pipe_read_ready,
   .except_ready = pipe_except_ready,
   .get_rready_cond = pipe_get_rready_cond,
   .get_except_cond = pipe_get_except_cond,
};

static const struct file_ops static_ops_pipe_write_end =
{
   .write = pipe_write,
   .except_ready = pipe_except_ready,
   .write_ready = pipe_write_ready,
   .get_wready_cond = pipe_get_wready_cond,
   .get_except_cond = pipe_get_except_cond,
};

void destroy_pipe(struct pipe *p)
{
   kcond_destory(&p->errcond);
   kcond_destory(&p->wcond);
   kcond_destory(&p->rcond);
   kmutex_destroy(&p->mutex);
   ringbuf_destory(&p->rb);
   kfree2(p->buf, PIPE_BUF_SIZE);
   kfree2(p, sizeof(struct pipe));
}

static void pipe_on_handle_close(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   int old;

   old = atomic_fetch_sub_explicit(
      (kh->fl_flags & O_WRONLY) ? &p->write_handles : &p->read_handles,
      1,
      mo_relaxed
   );

   ASSERT(old > 0);

   if (old == 1) {
      kcond_signal_all(&p->rcond);
      kcond_signal_all(&p->wcond);
      kcond_signal_all(&p->errcond);
   }
}

static void pipe_on_handle_dup(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;

   if (kh->fl_flags & O_WRONLY) {
      atomic_fetch_add_explicit(&p->write_handles, 1, mo_relaxed);
   } else {
      atomic_fetch_add_explicit(&p->read_handles, 1, mo_relaxed);
   }
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

   p->read_must_block = true;
   p->on_handle_close = &pipe_on_handle_close;
   p->on_handle_dup = &pipe_on_handle_dup;
   p->destory_obj = (void *)&destroy_pipe;
   ringbuf_init(&p->rb, PIPE_BUF_SIZE, 1, p->buf);
   kmutex_init(&p->mutex, 0);
   kcond_init(&p->rcond);
   kcond_init(&p->wcond);
   kcond_init(&p->errcond);
   return p;
}

fs_handle pipe_create_read_handle(struct pipe *p)
{
   fs_handle res = NULL;

   res = kfs_create_new_handle(&static_ops_pipe_read_end, (void *)p, O_RDONLY);

   if (res != NULL)
      atomic_fetch_add_explicit(&p->read_handles, 1, mo_relaxed);

   return res;
}

fs_handle pipe_create_write_handle(struct pipe *p)
{
   fs_handle res = NULL;

   res = kfs_create_new_handle(&static_ops_pipe_write_end, (void*)p, O_WRONLY);

   if (res != NULL)
      atomic_fetch_add_explicit(&p->write_handles, 1, mo_relaxed);

   return res;
}
