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
