/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

#define EMIT_UNALIGNED_READ_N(n)            \
   static ALWAYS_INLINE u##n                \
   unaligned_read_##n(void *ptr)            \
   {                                        \
      u##n val;                             \
      memcpy(&val, ptr, sizeof(val));       \
      return val;                           \
   }

#define EMIT_UNALIGNED_WRITE_N(n)                     \
   static ALWAYS_INLINE void                          \
   unaligned_write_##n(void *ptr, u##n val)           \
   {                                                  \
      memcpy(ptr, &val, sizeof(val));                 \
   }


EMIT_UNALIGNED_READ_N(16)
EMIT_UNALIGNED_READ_N(32)
EMIT_UNALIGNED_READ_N(64)

EMIT_UNALIGNED_WRITE_N(16)
EMIT_UNALIGNED_WRITE_N(32)
EMIT_UNALIGNED_WRITE_N(64)

#define get_field_ptr(obj, field_name) \
   ((char *)(obj) + offsetof(typeof(*(obj)), field_name))

#define read_unaligned_field(obj, field)                           \
   ({                                                              \
      typeof((obj)->field) result;                                 \
      memcpy(&result, get_field_ptr(obj, field), sizeof(result));  \
      result;                                                      \
   })

#define write_unaligned_field(obj, field, val)                     \
   ({                                                              \
      ASSERT(sizeof((obj)->field) == sizeof(val));                 \
      memcpy(get_field_ptr((obj), field), &val, sizeof(val));      \
   })
