/* SPDX-License-Identifier: BSD-2-Clause */

extern "C" {
   #include <tilck/common/basic_defs.h>
}

#ifndef __cplusplus
   #error This is a C++-only header. Use it from a C++ source file.
#endif

/*
 * Unfortunately, in C++11 there's no such thing as Dlang's "static if".
 * Therefore, even if in practice regular IFs behave like "static if"s when
 * their condition is known at compile-time, this happens at a lower level,
 * where dead code branches are discarded. At higher levels, we still must
 * provide an instantiation for every value of `size` we wanna support,
 * including our spacial '0'. Therefore, it's a useful hack to introduce
 * something like this `__bad_type`, in order to prevent accidental uses of
 * (un)signed_by_size<N>::type.
 */
struct __bad_type {
   __bad_type() = delete;
   __bad_type(const __bad_type&) = delete;
   __bad_type(__bad_type&&) = delete;
   ~__bad_type() = delete;
   __bad_type& operator=(__bad_type&&) = delete;
   __bad_type& operator=(const __bad_type&) { NOT_REACHED(); }
};

template <int size>
struct signed_by_size;

template <>
struct signed_by_size<0> {
   typedef __bad_type type;
};
template <>
struct signed_by_size<1> {
   typedef s8 type;
};
template <>
struct signed_by_size<2> {
   typedef s16 type;
};
template <>
struct signed_by_size<4> {
   typedef s32 type;
};
template <>
struct signed_by_size<8> {
   typedef s64 type;
};


template <int size>
struct unsigned_by_size;

template <>
struct unsigned_by_size<0> {
   typedef __bad_type type;
};
template <>
struct unsigned_by_size<1> {
   typedef u8 type;
};
template <>
struct unsigned_by_size<2> {
   typedef u16 type;
};
template <>
struct unsigned_by_size<4> {
   typedef u32 type;
};
template <>
struct unsigned_by_size<8> {
   typedef u64 type;
};

/* signed -> unsigned_type */

template <typename T>
struct unsigned_type;

template <>
struct unsigned_type<s8> {
   typedef u8 type;
};
template <>
struct unsigned_type<s16> {
   typedef u16 type;
};
template <>
struct unsigned_type<s32> {
   typedef u32 type;
};
template <>
struct unsigned_type<s64> {
   typedef u64 type;
};
#if NBITS == 32
template <>
struct unsigned_type<long> {
   typedef ulong type;
};
#endif


template <>
struct unsigned_type<u8> {
   typedef u8 type;
};
template <>
struct unsigned_type<u16> {
   typedef u16 type;
};
template <>
struct unsigned_type<u32> {
   typedef u32 type;
};
template <>
struct unsigned_type<u64> {
   typedef u64 type;
};
#if NBITS == 32
template <>
struct unsigned_type<ulong> {
   typedef ulong type;
};
#endif

/* is unsigned */

template <typename T>
struct is_unsigned {
   enum { val = 0 };
};

template <>
struct is_unsigned<u8> {
   enum { val = 1 };
};

template <>
struct is_unsigned<u16> {
   enum { val = 1 };
};

template <>
struct is_unsigned<u32> {
   enum { val = 1 };
};

template <>
struct is_unsigned<u64> {
   enum { val = 1 };
};

#if NBITS == 32
template <>
struct is_unsigned<ulong> {
   enum { val = 1 };
};
#endif

/* numeric limits */

template <typename T>
struct limits;

#define INST_LIMIT_TEMPL(T, m, M)                         \
   template <>                                            \
   struct limits<T> {                                     \
      static constexpr T min() { return m; };             \
      static constexpr T max() { return M; };             \
   }

INST_LIMIT_TEMPL(u8,  0, UCHAR_MAX);
INST_LIMIT_TEMPL(u16, 0, USHRT_MAX);
INST_LIMIT_TEMPL(u32, 0, UINT_MAX);
INST_LIMIT_TEMPL(u64, 0, ULLONG_MAX);

INST_LIMIT_TEMPL(s8,  CHAR_MIN, CHAR_MAX);
INST_LIMIT_TEMPL(s16, SHRT_MIN, SHRT_MAX);
INST_LIMIT_TEMPL(s32, INT_MIN,  INT_MAX);
INST_LIMIT_TEMPL(s64, LLONG_MIN, LLONG_MAX);

