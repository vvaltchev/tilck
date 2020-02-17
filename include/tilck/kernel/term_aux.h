/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/sync.h>

struct term_action;

struct term_rb_data {
   struct kmutex lock;
   struct safe_ringbuf rb;
};

void
init_term_rb_data(struct term_rb_data *d,
                  u16 max_elems,
                  u16 elem_size,
                  void *buf);

void
dispose_term_rb_data(struct term_rb_data *d);

void
term_unable_to_enqueue_action(term *t,
                              struct term_rb_data *d,
                              struct term_action *a,
                              bool *was_empty,
                              void (*exec_everything)(term *));
