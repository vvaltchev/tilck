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
   WOBJ_MULTI_ELEM  /*
                     * wobj part of a set containing multiple wait objects
                     * on which a given task is waiting. The wobj elem
                     * can NEVER be task_info->wobj. Instead, it is a member
                     * of a multi_wait_obj.
                     */
};

/*
 * wait_obj is used internally in task_info for referring to an object that
 * is blocking that task (keeping it in a sleep state).
 */

typedef struct {

   ATOMIC(void *) __ptr;
   enum wo_type type;
   list_node wait_list_node;

} wait_obj;

typedef struct {

   wait_obj wobj;
   struct task_info *ti;

} multi_wait_obj;


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

/*
 * The mutex implementation used for locking in kernel mode.
 */

typedef struct {

   uptr id;
   struct task_info *owner_task;
   u32 flags;
   u32 lock_count; // Valid when the mutex is recursive
   list wait_list;

} kmutex;

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

   uptr id;
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

