/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/list.h>

struct task_info;

enum wo_type {
   WOBJ_NONE    = 0,
   WOBJ_KMUTEX  = 1,
   WOBJ_KCOND   = 2,
   WOBJ_TASK    = 3,
   WOBJ_TIMER   = 4
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

void wait_obj_set(wait_obj *wo,
                  enum wo_type type,
                  void *ptr,
                  list_node *wait_list);

void *wait_obj_reset(wait_obj *wo);

static inline void *wait_obj_get_ptr(wait_obj *wo)
{
   return atomic_load_explicit(&wo->__ptr, mo_relaxed);
}

void task_set_wait_obj(struct task_info *ti,
                       enum wo_type type,
                       void *ptr,
                       list_node *wait_list);

void *task_reset_wait_obj(struct task_info *ti);

/*
 * The mutex implementation used for locking in kernel mode.
 */

typedef struct {

   uptr id;
   struct task_info *owner_task;
   u32 flags;
   u32 lock_count; // Valid when the mutex is recursive
   list_node wait_list;

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
   list_node wait_list;

} kcond;

#define KCOND_WAIT_FOREVER 0

void kcond_signal_int(kcond *c, bool all);
void kcond_signal_single(kcond *c, struct task_info *ti);

void kcond_init(kcond *c);
bool kcond_wait(kcond *c, kmutex *m, u32 timeout_ticks);
void kcond_destory(kcond *c);

static inline void kcond_signal_one(kcond *c)
{
   kcond_signal_int(c, false);
}

static inline void kcond_signal_all(kcond *c)
{
   kcond_signal_int(c, true);
}

