/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sync.h>

/* Reader-preferring R/W lock */

typedef struct {

   kmutex readers_lock;
   ksem writers_sem;
   int readers_count;

} rwlock_rp;

void rwlock_rp_init(rwlock_rp *r);
void rwlock_rp_destroy(rwlock_rp *r);
void rwlock_rp_shlock(rwlock_rp *r);
void rwlock_rp_shunlock(rwlock_rp *r);
void rwlock_rp_exlock(rwlock_rp *r);
void rwlock_rp_exunlock(rwlock_rp *r);
