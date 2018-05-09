
#pragma once
#include <common/basic_defs.h>

typedef struct {

   union {

      struct {
         u32 read_pos : 15;
         u32 write_pos : 15;
         u32 full : 1;
         u32 avail_bit : 1;
      };

      u32 raw;
   };

} generic_ringbuf_stat;

typedef struct {

   u16 max_elems;
   u16 elem_size;
   char *buf;
   generic_ringbuf_stat s;

} ringbuf;

void ringbuf_init(ringbuf *rb, u16 max_elems, u16 elem_size, void *buf);
void ringbuf_destory(ringbuf *rb);
bool ringbuf_write_elem(ringbuf *rb, void *elem_ptr);
bool ringbuf_read_elem(ringbuf *rb, void *elem_ptr /* out */);
