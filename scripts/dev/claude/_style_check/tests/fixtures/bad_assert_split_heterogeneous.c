/* SPDX-License-Identifier: BSD-2-Clause */

#define ASSERT(x) do { if (!(x)) abort(); } while (0)

struct task { int state; int ref_count; void *process; };
extern void abort(void);
extern int is_preemption_enabled(void);

void check(struct task *ti)
{
   /* 4-clause heterogeneous ASSERT: state + pointer + count +
    * function call. Fires the rule. */
   ASSERT(ti->state == 0 &&
          ti->process != 0 &&
          ti->ref_count > 0 &&
          is_preemption_enabled());
}

void single_clause_assert(int x)
{
   ASSERT(x > 0);             /* single clause: no fire */
}

void two_clause_assert(int x)
{
   ASSERT(x > 0 && x < 100);  /* 2 clauses: no fire (homogeneous range) */
}
