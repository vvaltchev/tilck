/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>

/* Reader-preferring rwlock */

struct rwlock_rp {

   kmutex readers_lock;
   ksem writers_sem;
   int readers_count;

#ifdef DEBUG
   struct task *ex_owner;
#endif

};

void rwlock_rp_init(struct rwlock_rp *r);
void rwlock_rp_destroy(struct rwlock_rp *r);
void rwlock_rp_shlock(struct rwlock_rp *r);
void rwlock_rp_shunlock(struct rwlock_rp *r);
void rwlock_rp_exlock(struct rwlock_rp *r);
void rwlock_rp_exunlock(struct rwlock_rp *r);

#ifdef DEBUG

   static inline bool rwlock_rp_is_shlocked(struct rwlock_rp *r)
   {
      return r->readers_count > 0;
   }

   static inline bool rwlock_rp_holding_exlock(struct rwlock_rp *r)
   {
      return r->ex_owner == get_curr_task();
   }

#endif

/* Writer-preferring rwlock */

typedef struct {

   struct task *ex_owner;
   kmutex m;
   kcond c;
   int r;     /* readers count */
   bool w;    /* writer waiting */
   bool rec;  /* is exlock operation recursive */
   u16 rc;    /* recursive locking count */

} rwlock_wp;

void rwlock_wp_init(rwlock_wp *rw, bool recursive);
void rwlock_wp_destroy(rwlock_wp *rw);
void rwlock_wp_shlock(rwlock_wp *rw);
void rwlock_wp_shunlock(rwlock_wp *rw);
void rwlock_wp_exlock(rwlock_wp *rw);
void rwlock_wp_exunlock(rwlock_wp *rw);

#ifdef DEBUG

   static inline bool rwlock_wp_is_shlocked(rwlock_wp *rw)
   {
      return rw->r > 0;
   }

   static inline bool rwlock_wp_holding_exlock(rwlock_wp *rw)
   {
      return rw->ex_owner == get_curr_task();
   }

#endif
