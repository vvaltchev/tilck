# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(GENERAL_DEFS_LIST "")

if (CMAKE_BUILD_TYPE STREQUAL "Release")

   message(STATUS "Preparing a RELEASE build...")
   list(APPEND GENERAL_DEFS_LIST "-DNDEBUG -DTILCK_RELEASE_BUILD")
   set(OPT_FLAGS_LIST -O3)

elseif (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")

   message(STATUS "Preparing a MinSizeRel build...")
   list(APPEND GENERAL_DEFS_LIST "-DNDEBUG -DTILCK_RELEASE_BUILD")
   set(OPT_FLAGS_LIST -Os)

elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")

   message(STATUS "Preparing a DEBUG build...")
   list(APPEND GENERAL_DEFS_LIST "-DTILCK_DEBUG_BUILD")
   set(OPT_FLAGS_LIST -O0 -fno-inline-functions)

else()

   message(FATAL_ERROR "Unknown build type: '${CMAKE_BUILD_TYPE}'")

endif()

if (TEST_GCOV OR KERNEL_GCOV)
   if (NOT EXISTS ${TCROOT}/noarch/lcov-${LCOV_VER})
      show_missing_lcov_error()
   endif()
endif()

if (TEST_GCOV)
   set(GCOV_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
   set(GCOV_LINK_FLAGS "-fprofile-arcs -lgcov")
endif()

set(
   DBG_FLAGS_LIST

   -ggdb
   -fno-omit-frame-pointer
)

set(
   WARN_FLAGS_LIST

   -Wall
   -Wextra
   -Werror
   -Wshadow
   -Wvla
   -Wcast-align
   -Wno-unused-function
   -Wno-unused-parameter
   -Wno-unused-label
)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")

   if (WCONV)
      show_wconv_warning()
   endif()

elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")

   list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")

   if (USE_SYSCC)
      show_clang_and_syscc_error()
   endif()

   if (NOT KERNEL_SYSCC)
      if (WCONV)
         show_wconv_warning()
      endif()
   endif()

endif()

set(
   FREESTANDING_FLAGS_LIST

   -ffreestanding
   -fno-builtin
)
JOIN("${FREESTANDING_FLAGS_LIST}" ${SPACE} FREESTANDING_FLAGS)

set(
   SAFER_BEHAVIOR_FLAGS_LIST

   -fno-strict-aliasing
   -fwrapv
   -Wundef
)
JOIN("${SAFER_BEHAVIOR_FLAGS_LIST}" ${SPACE} SAFER_BEHAVIOR_FLAGS)

JOIN("${GENERAL_DEFS_LIST}"       ${SPACE} GENERAL_DEFS)
JOIN("${DBG_FLAGS_LIST}"          ${SPACE} DBG_FLAGS)
JOIN("${OPT_FLAGS_LIST}"          ${SPACE} OPT_FLAGS)
JOIN("${WARN_FLAGS_LIST}"         ${SPACE} WARN_FLAGS)
set(COMMON_FLAGS "${GENERAL_DEFS} ${DBG_FLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")

# Kernel flags

set(GENERAL_KERNEL_FLAGS_LIST "")

list(
   APPEND GENERAL_KERNEL_FLAGS_LIST

   -D__TILCK_KERNEL__

   ${FREESTANDING_FLAGS_LIST}
   ${SAFER_BEHAVIOR_FLAGS_LIST}
)

if (${KERNEL_SYSCC} OR ${USE_SYSCC})
   if (CMAKE_C_COMPILER_ID STREQUAL "GNU")

      #
      # Unfortunately, when FORTIFY_SOURCE is enabled, new GCC compilers like
      # 9.x require the libc to implement extra functions and, apparently, they
      # don't care about -ffreestanding, nor care to put those functions in
      # libgcc, which Tilck links, statically.
      #
      # Therefore, in SYSCC builds, we fail with:
      #
      #     select.c:126: undefined reference to `__fdelt_chk'
      #
      # For the moment, it's OK to just disable those extra checks.
      # TODO: consider enabling _FORTIFY_SOURCE in Tilck.
      #

      list(
         APPEND GENERAL_KERNEL_FLAGS_LIST
         -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
      )

   endif()
endif()

if (WCONV)
   list(APPEND GENERAL_KERNEL_FLAGS_LIST "-Wconversion")
endif()

if (KERNEL_UBSAN)

   list(
      APPEND GENERAL_KERNEL_FLAGS_LIST

      -fsanitize=integer-divide-by-zero
      -fsanitize=nonnull-attribute,returns-nonnull-attribute
      -fsanitize=bool,enum
      -fsanitize=unreachable
      -fsanitize=null
      -fsanitize=shift-exponent
      -fsanitize=shift-base
   )

   #
   # Cannot enable -fsanitize=alignment, with Clang because it generates leaf
   # functions which misalign the stack pointer. What happens when get an IRQ
   # while running one of those functions? If UBSAN is enabled, we'll get
   # panic, otherwise everything is fine, because x86 allows unaligned access.
   #
   # See:
   #  - https://bugs.llvm.org/show_bug.cgi?id=49828
   #  - https://forum.osdev.org/viewtopic.php?f=13&t=42430&start=0
   #

   if (CMAKE_C_COMPILER_ID STREQUAL "GNU")

      list(
         APPEND GENERAL_KERNEL_FLAGS_LIST

         -fsanitize=alignment
         -fsanitize-recover=alignment
      )

   endif()

endif()

JOIN("${GENERAL_KERNEL_FLAGS_LIST}" ${SPACE} GENERAL_KERNEL_FLAGS)
set(KERNEL_FLAGS "${COMMON_FLAGS} ${GENERAL_KERNEL_FLAGS}")

set(
   KERNEL_NO_ARCH_FLAGS_LIST

   -DKERNEL_TEST
   -fPIC
   ${KERNEL_FLAGS}
   ${GCOV_COMPILE_FLAGS}
)

JOIN("${KERNEL_NO_ARCH_FLAGS_LIST}" ${SPACE} KERNEL_NO_ARCH_FLAGS)

set(
   KERNEL_CXX_FLAGS_LIST

   -fno-exceptions
   -fno-use-cxa-atexit
   -fno-rtti
)

JOIN("${KERNEL_CXX_FLAGS_LIST}" ${SPACE} KERNEL_CXX_FLAGS)

set(
   LOWLEVEL_BINARIES_FLAGS_LIST

   -fno-common
   -fno-exceptions
   -fno-stack-protector
   -fno-asynchronous-unwind-tables
   -mno-red-zone

   # Allow easier disassembly debugging
   # -mpush-args
   # -mno-accumulate-outgoing-args
   # -mno-stack-arg-probe
)
JOIN("${LOWLEVEL_BINARIES_FLAGS_LIST}" ${SPACE} LOWLEVEL_BINARIES_FLAGS)


#
# On x86, -mgeneral-regs-only is supported only by GCC >= 7.0.
# Clang supports it only on Aarch64 and the patch for making it generic
# has been abandoned: https://reviews.llvm.org/D38479
#
set(MGENERAL_REGS_ONLY_SUPPORTED OFF)

if (${KERNEL_SYSCC} OR ${USE_SYSCC})

   # Special case: we're using the system compiler
   if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set(MGENERAL_REGS_ONLY_SUPPORTED ON)
   endif()

else()

   # DEFAULT CASE: we're using a GCC compiler from our toolchain
   set(MGENERAL_REGS_ONLY_SUPPORTED ON)
endif()

# Disable the generation of any kind of FPU instructions
if (${MGENERAL_REGS_ONLY_SUPPORTED})

   list(APPEND DISABLE_FPU_FLAGS_LIST -mgeneral-regs-only)

else()

   list(
      APPEND DISABLE_FPU_FLAGS_LIST

      -mno-80387
      -mno-mmx
      -mno-3dnow
      -mno-sse
      -mno-sse2
      -mno-avx
   )

endif()

JOIN("${DISABLE_FPU_FLAGS_LIST}" ${SPACE} DISABLE_FPU_FLAGS)
