/* SPDX-License-Identifier: BSD-2-Clause */

#if defined(__GNUC__) && !defined(__clang__)

   #define COMPILER_NAME            "gcc"
   #define COMPILER_MAJOR           __GNUC__
   #define COMPILER_MINOR           __GNUC_MINOR__
   #define COMPILER_PATCHLEVEL      __GNUC_PATCHLEVEL__

#elif defined(__clang__)

   #define COMPILER_NAME            "clang"
   #define COMPILER_MAJOR           __clang_major__
   #define COMPILER_MINOR           __clang_minor__
   #define COMPILER_PATCHLEVEL      __clang_patchlevel__

#else

   #error Compiler not supported

#endif
