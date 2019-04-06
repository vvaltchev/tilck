/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sync.h>

/* Reader-preferring rwlock */

typedef struct {

   kmutex readers_lock;
   ksem writers_sem;
   int readers_count;

#ifdef DEBUG
   struct task_info *ex_owner;
#endif

} rwlock_rp;

void rwlock_rp_init(rwlock_rp *r);
void rwlock_rp_destroy(rwlock_rp *r);
void rwlock_rp_shlock(rwlock_rp *r);
void rwlock_rp_shunlock(rwlock_rp *r);
void rwlock_rp_exlock(rwlock_rp *r);
void rwlock_rp_exunlock(rwlock_rp *r);

/* Writer-preferring rwlock */

typedef struct {

   kmutex m;
   kcond c;
   int r;     /* readers count */
   bool w;    /* writer waiting */

#ifdef DEBUG
   struct task_info *ex_owner;
#endif

} rwlock_wp;

void rwlock_wp_init(rwlock_wp *rw);
void rwlock_wp_destroy(rwlock_wp *rw);
void rwlock_wp_shlock(rwlock_wp *rw);
void rwlock_wp_shunlock(rwlock_wp *rw);
void rwlock_wp_exlock(rwlock_wp *rw);
void rwlock_wp_exunlock(rwlock_wp *rw);
