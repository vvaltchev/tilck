/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/list.h>

struct task;

enum wo_type {

   WOBJ_NONE = 0,
   WOBJ_KMUTEX,
   WOBJ_KCOND,
   WOBJ_TASK,
   WOBJ_SEM,
   WOBJ_WORKER,

   /* Special "meta-object" types */
   WOBJ_MWO_WAITER, /* struct multi_obj_waiter */
   WOBJ_MWO_ELEM    /* a pointer to this wobj is castable to mwobj_elem */
};

#define NO_EXTRA                 0
#define WEXTRA_TASK_STOPPED      1
#define WEXTRA_TASK_CONTINUED    2

/*
 * wait_obj is used internally in struct task for referring to an object that
 * is blocking that task (keeping it in a sleep state).
 */

struct wait_obj {

   union {
      ATOMIC(void *) __ptr;          /* ptr to the object we're waiting for */
      long __data;                   /* data field (id) describing the obj  */
   };

   u32 extra;                        /* extra info about the waiting reason  */
   enum wo_type type;                /* type of the object we're waiting for */
   struct list_node wait_list_node;  /* node in waited object's waiting list */
};

/*
 * Struct used as element in `multi_obj_waiter` using `wait_obj` through
 * composition.
 */
struct mwobj_elem {

   struct wait_obj wobj;
   struct task *ti;         /* Task owning this wait obj */
   enum wo_type type;       /* Actual object type. NOTE: wobj.type cannot be
                             * used because it have to be equal to
                             * WOBJ_MULTI_ELEM. */
};

/*
 * Heap-allocated object on which struct task->wobj "waits" when the task is
 * waiting on multiple objects.
 *
 * How it works
 * ---------------
 *
 */
struct multi_obj_waiter {

   int count;                    /* number of `struct mwobj_elem` elements */
   struct mwobj_elem elems[];    /* variable-size array */
};

void wait_obj_set(struct wait_obj *wo,
                  enum wo_type type,
                  void *ptr_or_data,
                  u32 extra,
                  struct list *wait_list);

void *wait_obj_reset(struct wait_obj *wo);

static ALWAYS_INLINE void *
wait_obj_get_ptr(struct wait_obj *wo)
{
   return atomic_load_explicit(&wo->__ptr, mo_relaxed);
}

static ALWAYS_INLINE long
wait_obj_get_data(struct wait_obj *wo)
{
   return wo->__data;
}

void prepare_to_wait_on(enum wo_type type,
                        void *ptr_or_data,
                        u32 extra,
                        struct list *wait_list);

void *wake_up(struct task *ti);

struct multi_obj_waiter *allocate_mobj_waiter(int elems);
void free_mobj_waiter(struct multi_obj_waiter *w);
void mobj_waiter_reset(struct mwobj_elem *e);
void mobj_waiter_reset2(struct multi_obj_waiter *w, int index);
void mobj_waiter_set(struct multi_obj_waiter *w,
                     int index,
                     enum wo_type type,
                     void *ptr,
                     struct list *wait_list);

void prepare_to_wait_on_multi_obj(struct multi_obj_waiter *w);

/*
 * The semaphore implementation used for locking in kernel mode.
 */

struct ksem {

   int max;
   volatile int counter;
   struct list wait_list;
};

#define KSEM_NO_MAX                             -1
#define KSEM_WAIT_FOREVER                       -1
#define KSEM_NO_WAIT                             0

#define STATIC_KSEM_INIT(s, val, max)            \
   {                                             \
      .max = (max),                              \
      .counter = (val),                          \
      .wait_list = STATIC_LIST_INIT(s.wait_list),\
   }

void ksem_init(struct ksem *s, int val, int max);
void ksem_destroy(struct ksem *s);
int ksem_wait(struct ksem *s, int units, int timeout_ticks);
int ksem_signal(struct ksem *s, int units);

/*
 * The mutex implementation used for locking in kernel mode.
 */

struct kmutex {

   struct task *owner_task;
   u32 flags;
   u32 lock_count; // Valid when the mutex is recursive
   struct list wait_list;

#if KMUTEX_STATS_ENABLED
   u32 num_waiters;
   u32 max_num_waiters;
#endif
};

#define STATIC_KMUTEX_INIT(m, fl)                 \
   {                                              \
      .owner_task = NULL,                         \
      .flags = 0,                                 \
      .lock_count = 0,                            \
      .wait_list = STATIC_LIST_INIT(m.wait_list), \
   }

#define KMUTEX_FL_RECURSIVE                                (1 << 0)

#if KERNEL_SELFTESTS

   /*
    * Magic kmutex flag, existing only when self tests are compiled-in and
    * designed specifically for selftest_kmutex_ord_med(). See the comments
    * in se_kmutex.c for more about it.
    */
   #define KMUTEX_FL_ALLOW_LOCK_WITH_PREEMPT_DISABLED      (1 << 1)
#endif

void kmutex_init(struct kmutex *m, u32 flags);
void kmutex_lock(struct kmutex *m);
bool kmutex_trylock(struct kmutex *m);
void kmutex_unlock(struct kmutex *m);
void kmutex_destroy(struct kmutex *m);

#if DEBUG_CHECKS
bool kmutex_is_curr_task_holding_lock(struct kmutex *m);
#endif

/*
 * A basic implementation of condition variables similar to the pthread ones.
 */

struct kcond {

   struct list wait_list;
};

#define STATIC_KCOND_INIT(s)                     \
   {                                             \
      .wait_list = STATIC_LIST_INIT(s.wait_list),\
   }

#define KCOND_WAIT_FOREVER 0

void kcond_init(struct kcond *c);
void kcond_destory(struct kcond *c);
void kcond_signal_one(struct kcond *c);
void kcond_signal_all(struct kcond *c);
bool kcond_wait(struct kcond *c, struct kmutex *m, u32 timeout_ticks);
bool kcond_is_anyone_waiting(struct kcond *c);
