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
#include <tilck/kernel/sched.h>

struct pipe {

   KOBJ_BASE_FIELDS

   char *buf;
   struct ringbuf rb;
   struct kmutex mutex;
   struct kcond not_full_cond;
   struct kcond not_empty_cond;
   struct kcond err_cond;

   ATOMIC(int) read_handles;
   ATOMIC(int) write_handles;
};

static ssize_t pipe_read(fs_handle h, char *buf, size_t size)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool sig_pending = false;
   ssize_t rc = 0;

   if (!size)
      return 0;

   kmutex_lock(&p->mutex);

   while (true) {

      rc = (ssize_t)ringbuf_read_bytes(&p->rb, (u8 *)buf, size);

      if (rc)
         break; /* Everything is alright, we read something */

      if (atomic_load_explicit(&p->write_handles, mo_relaxed) == 0) {
         /* No more writers, always return 0, no matter what. */
         break;
      }

      if (kh->fl_flags & O_NONBLOCK) {
         rc = -EAGAIN;
         break;
      }

      /* Wait for writers to fill up the buffer */
      kcond_wait(&p->not_empty_cond, &p->mutex, KCOND_WAIT_FOREVER);

      /* After wake up */
      if (pending_signals()) {
         sig_pending = true;
         break;
      }
   }

   /*
    * Wake up one blocked writer instead of all of them.
    *
    * Rationale: it is totally possible that just a single writer will fill up
    * the whole buffer and, after that, the other writers will wake up just to
    * discover they need to go back sleeping again. To spare those unnecessary
    * context switches, we just wake up a single writer and, after it's done it
    * will wake up writer if the buffer is still not full.
    *
    * The situation is perfectly symmetric for the readers as well, that's why
    * here below we wake up another reader if the buffer is not empty.
    */
   kcond_signal_one(&p->not_full_cond);

   if (!ringbuf_is_empty(&p->rb)) {
      /* The buffer is not empty: wake up one more reader, if any */
      kcond_signal_one(&p->not_empty_cond);
   }

   /* Unlock the pipe's state lock and return */
   kmutex_unlock(&p->mutex);
   return !sig_pending ? rc : -EINTR;
}

static ssize_t pipe_write(fs_handle h, char *buf, size_t size)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool sig_pending = false;
   ssize_t rc = 0;

   if (!size)
      return 0;

   kmutex_lock(&p->mutex);

   while (true) {

      if (atomic_load_explicit(&p->read_handles, mo_relaxed) == 0) {

         /* Broken pipe */
         send_signal(get_curr_pid(), SIGPIPE, true);
         rc = -EPIPE;
         break;
      }

      rc = (ssize_t)ringbuf_write_bytes(&p->rb, (u8 *)buf, size);

      if (rc)
         break; /* Everything is alright, we wrote something */

      if (kh->fl_flags & O_NONBLOCK) {
         rc = -EAGAIN;
         break;
      }

      /* Wait for readers to empty the buffer */
      kcond_wait(&p->not_full_cond, &p->mutex, KCOND_WAIT_FOREVER);

      /* After wake up */
      if (pending_signals()) {
         sig_pending = true;
         break;
      }
   }

   /*
    * Wake up one blocked reader, instead of all of them.
    * See the comments in pipe_read() above.
    */
   kcond_signal_one(&p->not_empty_cond);

   if (!ringbuf_is_full(&p->rb)) {
      /* The buffer is not full: wake up one more writer, if any */
      kcond_signal_one(&p->not_full_cond);
   }

   /* Unlock the pipe's state lock and return */
   kmutex_unlock(&p->mutex);
   return !sig_pending ? rc : -EINTR;
}

static int pipe_read_ready(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool ret;

   kmutex_lock(&p->mutex);
   {
      ret = !ringbuf_is_empty(&p->rb) ||
            atomic_load_explicit(&p->write_handles, mo_relaxed) == 0;
   }
   kmutex_unlock(&p->mutex);
   return ret;
}

static struct kcond *pipe_get_rready_cond(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   return &p->not_empty_cond;
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

static struct kcond *pipe_get_wready_cond(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   return &p->not_full_cond;
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

static struct kcond *pipe_get_except_cond(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   return &p->err_cond;
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
   kcond_destory(&p->err_cond);
   kcond_destory(&p->not_empty_cond);
   kcond_destory(&p->not_full_cond);
   kmutex_destroy(&p->mutex);
   ringbuf_destory(&p->rb);
   kfree2(p->buf, PIPE_BUF_SIZE);
   kfree_obj(p, struct pipe);
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
      kcond_signal_all(&p->not_full_cond);
      kcond_signal_all(&p->not_empty_cond);
      kcond_signal_all(&p->err_cond);
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

   if (!(p = (void *)kzalloc_obj(struct pipe)))
      return NULL;

   if (!(p->buf = kmalloc(PIPE_BUF_SIZE))) {
      kfree_obj(p, struct pipe);
      return NULL;
   }

   p->on_handle_close = &pipe_on_handle_close;
   p->on_handle_dup = &pipe_on_handle_dup;
   p->destory_obj = (void *)&destroy_pipe;
   ringbuf_init(&p->rb, PIPE_BUF_SIZE, 1, p->buf);
   kmutex_init(&p->mutex, 0);
   kcond_init(&p->not_full_cond);
   kcond_init(&p->not_empty_cond);
   kcond_init(&p->err_cond);
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
