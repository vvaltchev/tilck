/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/list.h>

struct task_info;

enum wo_type {

   WOBJ_NONE = 0,
   WOBJ_KMUTEX,
   WOBJ_KCOND,
   WOBJ_TASK,
   WOBJ_SEM,

   /* Special "meta-object" types */

   WOBJ_MWO_WAITER, /* multi_obj_waiter */
   WOBJ_MWO_ELEM    /* a pointer to this wobj is castable to mwobj_elem */
};

/*
 * wait_obj is used internally in task_info for referring to an object that
 * is blocking that task (keeping it in a sleep state).
 */

typedef struct {

   ATOMIC(void *) __ptr;      /* pointer to the object we're waiting for */
   enum wo_type type;         /* type of the object we're waiting for */
   list_node wait_list_node;  /* node in waited object's waiting list */

} wait_obj;

/*
 * Struct used as element in `multi_obj_waiter` using `wait_obj` through
 * composition.
 */
typedef struct {

   wait_obj wobj;
   struct task_info *ti;    /* Task owning this wait obj */
   enum wo_type type;       /* Actual object type. NOTE: wobj.type cannot be
                             * used because it have to be equal to
                             * WOBJ_MULTI_ELEM. */

} mwobj_elem;

/*
 * Heap-allocated object on which task_info->wobj "waits" when the task is
 * waiting on multiple objects.
 *
 * How it works
 * ---------------
 *
 */
typedef struct {

   u32 count;             /* number of `mwobj_elem` elements */
   mwobj_elem elems[];    /* variable-size array */

} multi_obj_waiter;

/*
 * For a wait_obj with type == WOBJ_TASK, WOBJ_TASK_PTR_ANY_CHILD is a special
 * value for __ptr meaning that the task owning the wait_obj is going to wait
 * for any of its children to change state (usually, = to die).
 */
#define WOBJ_TASK_PTR_ANY_CHILD ((void *) -1)

void wait_obj_set(wait_obj *wo,
                  enum wo_type type,
                  void *ptr,
                  list *wait_list);

void *wait_obj_reset(wait_obj *wo);

static inline void *wait_obj_get_ptr(wait_obj *wo)
{
   return atomic_load_explicit(&wo->__ptr, mo_relaxed);
}

void task_set_wait_obj(struct task_info *ti,
                       enum wo_type type,
                       void *ptr,
                       list *wait_list);

void *task_reset_wait_obj(struct task_info *ti);

multi_obj_waiter *allocate_mobj_waiter(u32 elems);
void free_mobj_waiter(multi_obj_waiter *w);
void mobj_waiter_reset(mwobj_elem *e);
void mobj_waiter_reset2(multi_obj_waiter *w, u32 index);
void mobj_waiter_set(multi_obj_waiter *w,
                     u32 index,
                     enum wo_type type,
                     void *ptr,
                     list *wait_list);

void kernel_sleep_on_waiter(multi_obj_waiter *w);

/*
 * The semaphore implementation used for locking in kernel mode.
 */

typedef struct {

   int counter;
   list wait_list;

} ksem;

void ksem_init(ksem *s);
void ksem_destroy(ksem *s);
void ksem_wait(ksem *s);
void ksem_signal(ksem *s);

/*
 * The mutex implementation used for locking in kernel mode.
 */

typedef struct {

   struct task_info *owner_task;
   u32 flags;
   u32 lock_count; // Valid when the mutex is recursive
   list wait_list;

#if KMUTEX_STATS_ENABLED
   u32 num_waiters;
   u32 max_num_waiters;
#endif

} kmutex;

#define STATIC_KMUTEX_INIT(m, fl)                 \
   {                                              \
      .owner_task = NULL,                         \
      .flags = 0,                                 \
      .lock_count = 0,                            \
      .wait_list = make_list(m.wait_list),        \
   }

#define KMUTEX_FL_RECURSIVE (1 << 0)

void kmutex_init(kmutex *m, u32 flags);
void kmutex_lock(kmutex *m);
bool kmutex_trylock(kmutex *m);
void kmutex_unlock(kmutex *m);
void kmutex_destroy(kmutex *m);

#ifdef DEBUG
bool kmutex_is_curr_task_holding_lock(kmutex *m);
#endif

/*
 * A basic implementation of condition variables similar to the pthread ones.
 */

typedef struct {

   list wait_list;

} kcond;

#define KCOND_WAIT_FOREVER 0

void kcond_init(kcond *c);
void kcond_destory(kcond *c);
void kcond_signal_int(kcond *c, bool all);
bool kcond_wait(kcond *c, kmutex *m, u32 timeout_ticks);

static inline void kcond_signal_one(kcond *c)
{
   kcond_signal_int(c, false);
}

static inline void kcond_signal_all(kcond *c)
{
   kcond_signal_int(c, true);
}

