/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * With modern GCC compilers, things like *(u32 *)(ptr + off) are undefined
 * behavior if `ptr+off` is not aligned correctly, even on architectures
 * like x86 that natively support unaligned access. That's because in the C
 * standard (since C89) unaligned access is considered UB.
 *
 * From C11 (draft N1570):
 * <<
 *    Conversion between two pointer types produces a result that is
 *    incorrectly aligned (6.3.2.3).
 * >>
 *
 * See GCC's bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=93031
 *
 * The correct solution to read/write values where the address might be
 * unaligned is to use __builtin_memcpy(): it will just emit a single MOV
 * instruction on x86, while it could do more on architectures where unaligned
 * access is not allowed.
 */

#define SAFE_READ(addr, type) ({                                \
   type __rres;                                                 \
   __builtin_memcpy(&__rres, (void *)(addr), sizeof(__rres));   \
   __rres;                                                      \
})

#define SAFE_WRITE(addr, type, val) ({                          \
   const type __wtmp = (val);                                   \
   __builtin_memcpy((void *)(addr), &__wtmp, sizeof(__wtmp));   \
})

#define READ_U16(addr)          SAFE_READ((addr), u16)
#define READ_S16(addr)          SAFE_READ((addr), s16)
#define READ_U32(addr)          SAFE_READ((addr), u32)
#define READ_S32(addr)          SAFE_READ((addr), s32)
#define READ_U64(addr)          SAFE_READ((addr), u64)
#define READ_S64(addr)          SAFE_READ((addr), s64)
#define READ_ULONG(addr)        SAFE_READ((addr), ulong)
#define READ_LONG(addr)         SAFE_READ((addr), long)
#define READ_PTR(addr)          SAFE_READ((addr), void *)

#define WRITE_U16(addr, val)    SAFE_WRITE((addr), u16, (val))
#define WRITE_S16(addr, val)    SAFE_WRITE((addr), s16, (val))
#define WRITE_U32(addr, val)    SAFE_WRITE((addr), u32, (val))
#define WRITE_S32(addr, val)    SAFE_WRITE((addr), s32, (val))
#define WRITE_U64(addr, val)    SAFE_WRITE((addr), u64, (val))
#define WRITE_S64(addr, val)    SAFE_WRITE((addr), s64, (val))
#define WRITE_ULONG(addr, val)  SAFE_WRITE((addr), ulong, (val))
#define WRITE_LONG(addr, val)   SAFE_WRITE((addr), long, (val))
#define WRITE_PTR(addr, val)    SAFE_WRITE((addr), void *, (val))

#define GET_FIELD_PTR(obj, field_name) \
   ((char *)(obj) + offsetof(typeof(*(obj)), field_name))

#define READ_FIELD(obj, field) ({                                            \
   typeof((obj)->field) result;                                              \
   __builtin_memcpy(&result, GET_FIELD_PTR(obj, field), sizeof(result));     \
   result;                                                                   \
})

#define WRITE_FIELD(obj, field, val) ({                                      \
   ASSERT(sizeof((obj)->field) == sizeof(val));                              \
   __builtin_memcpy(GET_FIELD_PTR((obj), field), &val, sizeof(val));         \
})
