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

   bool read_must_block;

   ATOMIC(int) read_handles;
   ATOMIC(int) write_handles;
};

static ssize_t pipe_read(fs_handle h, char *buf, size_t size)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   size_t rc = 0;

   if (!size)
      return 0;

   kmutex_lock(&p->mutex);
   {
   again:

      if (!(rc = ringbuf_read_bytes(&p->rb, (u8 *)buf, size))) {

         if (atomic_load_explicit(&p->write_handles, mo_relaxed) == 0) {
            /* No more writers, always return 0, no matter what. */
            goto end;
         }

         if (p->read_must_block) {

            kcond_wait(&p->wcond, &p->mutex, KCOND_WAIT_FOREVER);
            goto again;

         } else {

            p->read_must_block = true;
            goto end;
         }
      }

      kcond_signal_one(&p->rcond);
   end:;
   }
   kmutex_unlock(&p->mutex);
   return (ssize_t)rc;
}

static ssize_t pipe_write(fs_handle h, char *buf, size_t size)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   ssize_t rc = 0;

   if (!size)
      return 0;

   kmutex_lock(&p->mutex);
   {
   again:

      if (atomic_load_explicit(&p->read_handles, mo_relaxed) == 0) {

         /* Broken pipe */

         int pid = get_curr_pid();
         send_signal(pid, SIGPIPE, true);

         rc = -EPIPE;
         goto end;
      }

      if (!(rc = (ssize_t)ringbuf_write_bytes(&p->rb, (u8 *)buf, size))) {
         kcond_wait(&p->rcond, &p->mutex, KCOND_WAIT_FOREVER);
         goto again;
      }

      p->read_must_block = false;
      kcond_signal_one(&p->wcond);

   end:;
   }
   kmutex_unlock(&p->mutex);
   return rc;
}

static bool pipe_read_ready(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool ret;

   kmutex_lock(&p->mutex);
   {
      ret = !ringbuf_is_empty(&p->rb);
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

static bool pipe_write_ready(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool ret;

   kmutex_lock(&p->mutex);
   {
      ret = !ringbuf_is_full(&p->rb);
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

static const struct file_ops static_ops_pipe_read_end =
{
   .read = pipe_read,
   .read_ready = pipe_read_ready,
   .get_rready_cond = pipe_get_rready_cond,
};

static const struct file_ops static_ops_pipe_write_end =
{
   .write = pipe_write,
   .write_ready = pipe_write_ready,
   .get_wready_cond = pipe_get_wready_cond,
};

void destroy_pipe(struct pipe *p)
{
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

   if (kh->fl_flags & O_WRONLY) {

      DEBUG_ONLY_UNSAFE(int old =)
         atomic_fetch_sub_explicit(&p->write_handles, 1, mo_relaxed);

      ASSERT(old > 0);

   } else {

      DEBUG_ONLY_UNSAFE(int old =)
         atomic_fetch_sub_explicit(&p->read_handles, 1, mo_relaxed);

      ASSERT(old > 0);
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
