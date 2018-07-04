
#pragma once

#include <exos/common/basic_defs.h>

typedef struct task_info task_info;

typedef enum {
   WOBJ_NONE    = 0,
   WOBJ_KMUTEX  = 1,
   WOBJ_KCOND   = 2,
   WOBJ_TASK    = 3,
   WOBJ_TIMER   = 4
} wo_type;

/*
 * wait_obj is used internally in task_info for referring to an object that
 * is blocking that task (keeping it in a sleep state).
 */

typedef struct {

   wo_type type;
   void *ptr;

} wait_obj;

static inline void wait_obj_set(wait_obj *obj, wo_type type, void *ptr)
{
   obj->type = type;
   obj->ptr = ptr;
}

static inline void wait_obj_reset(wait_obj *obj)
{
   obj->type = WOBJ_NONE;
   obj->ptr = NULL;
}

/*
 * The mutex implementation used for locking in kernel mode.
 */

typedef struct {

   uptr id;
   task_info *owner_task;
   u32 flags;
   u32 lock_count; // Valid when the mutex is recursive

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
   int timer_num;

} kcond;

#define KCOND_WAIT_FOREVER 0

void kcond_signal_int(kcond *c, bool all);
void kcond_signal_single(kcond *c, task_info *ti);

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

