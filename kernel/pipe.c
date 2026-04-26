/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/pipe.h>
#include <tilck/kernel/fs/kernelfs.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>

#if KRN_HANG_DETECTION
   #include <tilck/kernel/list.h>
   #include <tilck/kernel/timer.h>

/*
 * Per-pipe ring of recent {init, dup, close} events. The hang detector
 * dumps this alongside the pipe's read/write handle counts when a stuck
 * task is found waiting on the pipe's not_empty/not_full cond — it
 * makes the "who closed which end and when" picture obvious without
 * needing to interleave a bunch of per-syscall printks. 32 entries is
 * enough to cover a typical pipeline lifecycle plus fork-inheritance
 * noise.
 */
#define PIPE_HISTORY 32

struct pipe_event {
   u64 ts;                       /* tick counter when the op happened    */
   int tid;                      /* tid that performed the op            */
   char op;                      /* 'I'=init, 'D'=dup, 'C'=close         */
   bool is_write;                /* RD vs WR end                         */
   u16 read_handles_after;       /* counters as observed AFTER the op,   */
   u16 write_handles_after;      /* so the trail is self-explaining      */
};
#endif /* KRN_HANG_DETECTION */

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

#if KRN_HANG_DETECTION
   /* Hang-detector bookkeeping. Off the fast path; only touched
    * inside dup/close hooks and walked by the dump helper. */
   struct list_node all_pipes_node;
   struct pipe_event history[PIPE_HISTORY];
   u8 history_idx;
#endif
};

#if KRN_HANG_DETECTION
/*
 * Global registry of every live pipe. Walked by
 * debug_dump_pipe_state_for_obj() to find a pipe by one of its cond /
 * mutex pointers when a stuck task is waiting on it. Mutation
 * (add / remove on create / destroy) is protected by disable_preemption,
 * which is enough because Tilck is single-CPU and the readers (dump)
 * also run preempt-disabled.
 */
static struct list all_pipes = STATIC_LIST_INIT(all_pipes);

/*
 * Append one entry to the pipe's ring buffer. Caller MUST already hold
 * p->mutex *or* be operating during pipe construction (no concurrent
 * access yet). The dup-side caller is the one exception: it cannot
 * take the mutex (see comment in pipe_on_handle_dup) so its snapshot
 * may race slightly with a concurrent close — accepted as a debug-only
 * inaccuracy.
 */
static void
record_pipe_event(struct pipe *p, char op, bool is_write)
{
   struct pipe_event *ev = &p->history[p->history_idx % PIPE_HISTORY];

   ev->ts = get_ticks();
   ev->tid = get_curr_tid();
   ev->op = op;
   ev->is_write = is_write;
   ev->read_handles_after = (u16)p->read_handles;
   ev->write_handles_after = (u16)p->write_handles;
   p->history_idx++;
}
#endif /* KRN_HANG_DETECTION */

static ssize_t pipe_read(fs_handle h, char *buf, size_t size, offt *pos)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool sig_pending = false;
   ssize_t rc = 0;
   ASSERT(*pos == 0);

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

static ssize_t pipe_write(fs_handle h, char *buf, size_t size, offt *pos)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool sig_pending = false;
   ssize_t rc = 0;
   ASSERT(*pos == 0);

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
#if KRN_HANG_DETECTION
   disable_preemption();
   {
      list_remove(&p->all_pipes_node);
   }
   enable_preemption();
#endif

   kcond_destroy(&p->err_cond);
   kcond_destroy(&p->not_empty_cond);
   kcond_destroy(&p->not_full_cond);
   kmutex_destroy(&p->mutex);
   ringbuf_destory(&p->rb);
   kfree2(p->buf, PIPE_BUF_SIZE);
   kfree_obj(p, struct pipe);
}

#if KRN_HANG_DETECTION

struct pipe *debug_get_pipe_for_handle(fs_handle h, bool *is_write_end)
{
   struct fs_handle_base *hb = h;
   struct kfs_handle *kh;

   if (!h)
      return NULL;

   /* Discriminate by file_ops pointer comparison: kfs_handle is shared
    * across all kernelfs object kinds, but the fops table is unique
    * per pipe end. Pipes are the only kernelfs consumer today; if
    * another one shows up, this stays correct because we explicitly
    * test against pipe-specific fops only. */
   if (hb->fops == &static_ops_pipe_read_end) {
      *is_write_end = false;
   } else if (hb->fops == &static_ops_pipe_write_end) {
      *is_write_end = true;
   } else {
      return NULL;
   }

   kh = (void *)h;
   return (struct pipe *)kh->kobj;
}

void debug_dump_pipe_state_for_obj(void *obj)
{
   struct pipe *p;

   ASSERT(!is_preemption_enabled());

   list_for_each_ro(p, &all_pipes, all_pipes_node) {

      if (obj != &p->not_empty_cond &&
          obj != &p->not_full_cond  &&
          obj != &p->err_cond       &&
          obj != &p->mutex)
      {
         continue;
      }

      const char *which =
         obj == &p->not_empty_cond ? "not_empty_cond" :
         obj == &p->not_full_cond  ? "not_full_cond"  :
         obj == &p->err_cond       ? "err_cond"       :
                                     "mutex";

      printk(NO_PREFIX "    pipe(%p) [%s]: read_handles=%d write_handles=%d "
             "rb_used=%zu/%u\n",
             p, which,
             p->read_handles, p->write_handles,
             ringbuf_get_elems(&p->rb), (unsigned)PIPE_BUF_SIZE);

      /* Replay the per-pipe event ring in chronological order
       * (oldest first). Empty slots (op == 0) are pre-recording
       * leftovers from kzalloc; skip them. */
      for (int i = 0; i < PIPE_HISTORY; i++) {
         u8 idx = (u8)((p->history_idx + i) % PIPE_HISTORY);
         struct pipe_event *ev = &p->history[idx];
         if (ev->op == 0)
            continue;
         printk(NO_PREFIX
                "      ts=%llu tid=%d op=%c %s -> r=%u w=%u\n",
                (unsigned long long)ev->ts, ev->tid, ev->op,
                ev->is_write ? "WR" : "RD",
                (unsigned)ev->read_handles_after,
                (unsigned)ev->write_handles_after);
      }
   }
}

#endif /* KRN_HANG_DETECTION */

static void pipe_on_handle_close(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   int old;

   /*
    * Take p->mutex around BOTH the handle-count decrement and the wakeup
    * broadcast. Every reader/writer in pipe_read()/pipe_write() checks
    * read_handles/write_handles inside this same mutex right before deciding
    * to call kcond_wait(). If we decremented and signalled outside the
    * mutex, a reader that had just observed write_handles > 0 and was about
    * to enter kcond_wait could be preempted between the predicate check and
    * the wait; this close path would then signal an empty wait_list, and
    * the reader would later sleep forever (lost wakeup -> tid stuck waiting
    * on an EOF that nobody will ever broadcast).
    */
   kmutex_lock(&p->mutex);
   {
      old = atomic_fetch_sub_explicit(
         (kh->fl_flags & O_WRONLY) ? &p->write_handles : &p->read_handles,
         1,
         mo_relaxed
      );

      ASSERT(old > 0);

#if KRN_HANG_DETECTION
      record_pipe_event(p, 'C', !!(kh->fl_flags & O_WRONLY));
#endif

      if (old == 1) {
         kcond_signal_all(&p->not_full_cond);
         kcond_signal_all(&p->not_empty_cond);
         kcond_signal_all(&p->err_cond);
      }
   }
   kmutex_unlock(&p->mutex);
}

static void pipe_on_handle_dup(fs_handle h)
{
   struct kfs_handle *kh = h;
   struct pipe *p = (void *)kh->kobj;
   bool is_write = !!(kh->fl_flags & O_WRONLY);

   /*
    * MUST NOT take p->mutex here. This callback runs inside fork's
    * fork_dup_all_handles(), which executes with preemption disabled
    * (do_fork holds disable_preemption around the whole handle copy).
    * kmutex_lock() can block, and a blocking call from a preempt-disabled
    * context trips ASSERT(get_preempt_disable_count()==1) inside
    * save_regs_and_schedule().
    *
    * The atomic increment is correct on its own; the per-pipe history
    * snapshot below races slightly with a concurrent close (the recorded
    * read_handles/write_handles snapshot may be off by a concurrent
    * decrement) but that's acceptable for a debug aid.
    */
   if (is_write)
      atomic_fetch_add_explicit(&p->write_handles, 1, mo_relaxed);
   else
      atomic_fetch_add_explicit(&p->read_handles, 1, mo_relaxed);

#if KRN_HANG_DETECTION
   record_pipe_event(p, 'D', is_write);
#endif
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

#if KRN_HANG_DETECTION
   /* history[] zero-initialized by kzalloc_obj; history_idx starts at 0 */
   list_node_init(&p->all_pipes_node);

   /* Record creation event (no concurrent access yet, no need for mutex) */
   record_pipe_event(p, 'I', false);

   disable_preemption();
   {
      list_add_tail(&all_pipes, &p->all_pipes_node);
   }
   enable_preemption();
#endif

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
