/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * SAFE_RINGBUF: a reentrant ring buffer implementation designed as an efficient
 * non-interrupt blocking mechanism to transfer data between IRQ handlers and
 * regular (preemptable) kernel code.
 *
 * What is SAFE to do:
 *
 *    - Produce data (by writing to the ring buffer) from interrupt handlers
 *      code (nested interrupts supported).
 *
 *    - Consume data (by reading from the ring buffer) from preemptable code in
 *      process context. Such code must NEVER be able to preempt/interrupt
 *      the producer code. It always must be the other way around.
 *
 * In other words:
 *
 *    Producer code might interrupt consumer code or producer code. Consumer
 *    code must NEVER interrupt producer code, otherwise it might ready corrupt
 *    data.
 *
 * What is NOT safe to do:
 *
 *    - Have a consumer that interrupted a producer, no matter the context.
 *      The safe ring buffer is "safe" only in one direction, as explained
 *      above.
 *
 *    - Use the safe_ringbuf in a SMP machine (Tilck does not support that now)
 *      without proper external protection (spin locks).
 *
 * Usage example:
 *
 *    - vterm_write() creates an action and calls
 *      term_execute_or_enqueue_action().
 *
 *    - term_execute_or_enqueue_action() calls
 *      term_execute_or_enqueue_action_template().
 *
 *    - term_execute_or_enqueue_action_template() writes to the ring buffer and
 *      ONLY in the case `was_empty` was true, starts acting like a consumer
 *      by calling term_exec_everything() which reads from the buffer in a loop.
 *
 * It doesn't matter the context IRQ (handler or not) in which vterm_write() was
 * called. The point is that only the task that wrote to the ringbuffer first
 * and found it empty becomes the consumer. All the other tasks or nested
 * interrupt handlers, interrupting the consumer will just write to the ring
 * buffer and exit. Wait a second: how on earth IRQ handlers will end up calling
 * vterm_write()? Only one way: if they call printk (they shouldn't but can) and
 * printk's buffer got full and it has no other choice than to flush directly
 * its to buffer to the term. Pretty unlikely, but possible.
 */


#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

struct generic_safe_ringbuf_stat {

   union {

      struct {
         u32 read_pos : 15;
         u32 write_pos : 15;
         u32 full : 1;
         u32 avail_bit : 1;
      };

      ATOMIC(u32) raw;
      u32 __raw;
   };

};

struct safe_ringbuf {

   u16 max_elems;
   u16 elem_size;
   struct generic_safe_ringbuf_stat s;
   u8 *buf;

#if DEBUG_CHECKS
   ATOMIC(int) nested_writes;
#endif
};

bool safe_ringbuf_is_empty(struct safe_ringbuf *rb);
bool safe_ringbuf_is_full(struct safe_ringbuf *rb);

void
safe_ringbuf_init(struct safe_ringbuf *rb, u16 max_elems, u16 e_size, void *b);

void
safe_ringbuf_destory(struct safe_ringbuf *rb);

/* Generic read/write funcs */

bool
safe_ringbuf_write_elem(struct safe_ringbuf *rb, void *e, bool *was_empty);

bool
safe_ringbuf_read_elem(struct safe_ringbuf *rb, void *elem_ptr /* out */);


/* Pointer-size read/write funcs */

bool
safe_ringbuf_write_ulong(struct safe_ringbuf *rb, void *e, bool *was_empty);

bool
safe_ringbuf_read_ulong(struct safe_ringbuf *rb, void *elem_ptr /* out */);


/* Size-specific read/write funcs */

bool safe_ringbuf_write_1(struct safe_ringbuf *rb, void *e, bool *was_empty);
bool safe_ringbuf_write_2(struct safe_ringbuf *rb, void *e, bool *was_empty);
bool safe_ringbuf_write_4(struct safe_ringbuf *rb, void *e, bool *was_empty);
bool safe_ringbuf_write_8(struct safe_ringbuf *rb, void *e, bool *was_empty);

bool safe_ringbuf_read_1(struct safe_ringbuf *rb, void *elem_ptr /* out */);
bool safe_ringbuf_read_2(struct safe_ringbuf *rb, void *elem_ptr /* out */);
bool safe_ringbuf_read_4(struct safe_ringbuf *rb, void *elem_ptr /* out */);
bool safe_ringbuf_read_8(struct safe_ringbuf *rb, void *elem_ptr /* out */);
